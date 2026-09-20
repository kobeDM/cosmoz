__version__ = "2026.08.11-v4"

import os
import struct
import datetime
import mmap
from collections.abc import Sequence

import numpy as np


# Cosmo-Z wave file reader
#
# Current header format written by daq.cpp:
#   double   timestamp       8
#   uint32   startTime       4
#   uint64   startTimeFPGA   8
#   uint16   capMode         2
#   uint32   channelMask     4
#   uint32   capLength       4
#   uint32   elapsedTime     4
#   double   liveRate[8]    64   <-- added in 2026 version
#   uint32   nEvent          4
#
# Event mode:
#   one callback == one event == one 24-byte event header + waveform(+padding)
#
# All integer data are little-endian on Cosmo-Z (Zynq/Linux).


_NCH_MAX = 8
_BASE_HEADER = struct.Struct("<dIQHIII")  # through elapsedTime (34 bytes)
_U32 = struct.Struct("<I")
_U64 = struct.Struct("<Q")
_U16 = struct.Struct("<H")
_EVENT_HEAD5 = struct.Struct("<5H")       # hdr, length, time_lo, time_mid, time_hi


class CZFormatError(RuntimeError):
    pass


def _normalise_range(length, start=0, end=-1):
    """Return a Python-slice style [start, end) range."""
    if start < 0:
        start = 0
    if end < 0 or end > length:
        end = length
    if start > end:
        start = end
    return start, end


class CZEvent:
    """One Cosmo-Z event.

    In Capture Mode, one record contains all active channels simultaneously.
    ``ch`` therefore stores the physical channel numbers as a list, e.g.
    ``[1, 8]``.  ``ch[i]``, ``ped[i]`` and ``wf[i]`` refer to the same channel.

    In Event Mode, one file record belongs to one physical channel.  ``ch``
    stores that physical channel number (1..8).  For backward compatibility,
    ``wf``/``ped`` are still lists of length ``nch``; only the corresponding
    active-channel slot is populated.
    """

    __slots__ = (
        "time", "length", "ch", "ped", "wf",
        "header", "_active_channels",
    )

    def __init__(self, *, time=0, length=0, ch=None, ped=None, wf=None,
                 header=None, active_channels=()):
        self.time = int(time)
        self.length = int(length)
        self.ch = ch
        self.ped = ped if ped is not None else []
        self.wf = wf if wf is not None else []
        self.header = header
        self._active_channels = tuple(active_channels)

    def max(self, ich=0, start=0, end=-1):
        a = np.asarray(self.wf[ich])
        start, end = _normalise_range(a.size, start, end)
        if start == end:
            raise ValueError("empty waveform range")
        return a[start:end].max().item()

    def min(self, ich=0, start=0, end=-1):
        a = np.asarray(self.wf[ich])
        start, end = _normalise_range(a.size, start, end)
        if start == end:
            raise ValueError("empty waveform range")
        return a[start:end].min().item()

    def integral(self, ich=0, ped=-1, start=0, end=-1):
        a = np.asarray(self.wf[ich])
        start, end = _normalise_range(a.size, start, end)
        if ped < 0:
            ped = self.ped[ich]
        # int64 avoids uint16 wrap-around.
        return int(a[start:end].astype(np.int64, copy=False).sum()
                   - int(ped) * (end - start))

    def num_less(self, ich, thrs, ped=-1, start=0, end=-1):
        a = np.asarray(self.wf[ich])
        start, end = _normalise_range(a.size, start, end)
        if ped < 0:
            ped = self.ped[ich]
        return int(np.count_nonzero(
            a[start:end].astype(np.int32, copy=False) - int(ped) < thrs
        ))

    def num_more(self, ich, thrs, ped=-1, start=0, end=-1):
        a = np.asarray(self.wf[ich])
        start, end = _normalise_range(a.size, start, end)
        if ped < 0:
            ped = self.ped[ich]
        return int(np.count_nonzero(
            a[start:end].astype(np.int32, copy=False) - int(ped) > thrs
        ))

    def time_sec(self):
        return 1.0e-8 * self.time

    def time_msec(self):
        return 1.0e-5 * self.time

    def time_usec(self):
        return 1.0e-2 * self.time

    def print(self):
        print("time:", self.time)
        print("length:", self.length)
        if self.ch is not None:
            print("channel:", self.ch)
        print("ped:", self.ped)
        print("wf:", self.wf)


class _BulkEventSequence(Sequence):
    """Lazy CZEvent facade over arrays created by read_all()."""

    def __init__(self, data):
        self._data = data

    def __len__(self):
        return self._data.nEventRead

    def __getitem__(self, i):
        n = len(self)
        if isinstance(i, slice):
            return [self[j] for j in range(*i.indices(n))]
        if i < 0:
            i += n
        if i < 0 or i >= n:
            raise IndexError(i)
        return self._data._bulk_event(i)

    def __iter__(self):
        for i in range(len(self)):
            yield self._data._bulk_event(i)


class _VariableWaveformSequence(Sequence):
    """Lazy waveform views for variable-length Event Mode data."""

    def __init__(self, data):
        self._data = data

    def __len__(self):
        return self._data.nEventRead

    def __getitem__(self, i):
        n = len(self)
        if isinstance(i, slice):
            return [self[j] for j in range(*i.indices(n))]
        if i < 0:
            i += n
        if i < 0 or i >= n:
            raise IndexError(i)

        d = self._data
        off = int(d._wave_offset[i])
        length = int(d.lengths[i])
        return np.ndarray(
            shape=(length,), dtype="<u2",
            buffer=d._bulk_buffer, offset=off
        )


class CZData:
    """Reader for Cosmo-Z ``.wave`` files.

    Parameters
    ----------
    fileName : str or path-like
    has_livetime : {True, False, "auto"}, default "auto"
        Current DAQ files contain 8 doubles (liveRate[8]) in the header.
        "auto" also recognises the older header without these 64 bytes.
    buffer_size : int
        Buffer used by streaming ``read()``.
    """

    def __init__(self, fileName, has_livetime="auto",
                 buffer_size=8 * 1024 * 1024, use_mmap=True):
        self.fileName = os.fspath(fileName)
        self.activeChannel = []
        self.events = []
        self.nEvent = 0
        self.nEventRead = 0
        self.isOpen = False

        self.timestamp = 0.0
        self.time = 0
        self.timeFPGA = 0
        self.capMode = 0
        self.channelMask = 0
        self.capLength = 0
        self.capTime = 0              # elapsed time [ms]
        self.liveRate = np.ones(_NCH_MAX, dtype=np.float64)
        self.liveTime = self.liveRate # alias
        self.hasLiveTime = False

        # Bulk-mode arrays.  Created by read_all().
        self.times = None
        self.lengths = None
        self.channels = None
        self.pedestals = None
        self.waveforms = None
        self.pedestalsByChannel = None
        self.waveformsByChannel = None

        self._bulk_buffer = None
        self._wave_offset = None
        self._record_size = None
        self._data_offset = None
        self._read_index = 0
        self._stream_pos = 0
        self._mmap = None
        self._use_mmap = bool(use_mmap)

        try:
            # A large BufferedReader makes per-event read() much faster while
            # keeping only the current event payload in Python memory.
            self.__fp = open(self.fileName, "rb", buffering=buffer_size)
            self.isOpen = True
            self._read_header(has_livetime)
            if self._use_mmap:
                # mmap maps the file into virtual address space but does not
                # read the whole file into Python RAM. Pages are loaded by the
                # OS on demand. Each read() still copies only one event payload.
                self._mmap = mmap.mmap(
                    self.__fp.fileno(), 0, access=mmap.ACCESS_READ
                )
        except FileNotFoundError:
            raise
        except Exception:
            if self.isOpen:
                self.__fp.close()
                self.isOpen = False
            raise

    # ------------------------------------------------------------------
    # Header
    # ------------------------------------------------------------------
    def _read_header(self, has_livetime):
        fp = self.__fp
        fp.seek(0)
        base = fp.read(_BASE_HEADER.size)
        if len(base) != _BASE_HEADER.size:
            raise CZFormatError("file is shorter than Cosmo-Z base header")

        (self.timestamp,
         self.time,
         self.timeFPGA,
         self.capMode,
         self.channelMask,
         self.capLength,
         self.capTime) = _BASE_HEADER.unpack(base)

        self.activeChannel = [
            i + 1 for i in range(_NCH_MAX)
            if (self.channelMask >> i) & 1
        ]
        self._active_index = {
            ch: i for i, ch in enumerate(self.activeChannel)
        }

        if has_livetime not in (True, False, "auto"):
            raise ValueError('has_livetime must be True, False, or "auto"')

        if has_livetime == "auto":
            has_livetime = self._detect_livetime_header()

        fp.seek(_BASE_HEADER.size)
        if has_livetime:
            raw = fp.read(8 * _NCH_MAX)
            if len(raw) != 8 * _NCH_MAX:
                raise CZFormatError("truncated liveRate header")
            self.liveRate = np.frombuffer(raw, dtype="<f8", count=_NCH_MAX).copy()
            self.liveTime = self.liveRate
            self.hasLiveTime = True
        else:
            self.liveRate = np.ones(_NCH_MAX, dtype=np.float64)
            self.liveTime = self.liveRate
            self.hasLiveTime = False

        raw = fp.read(4)
        if len(raw) != 4:
            raise CZFormatError("truncated nEvent header")
        self.nEvent = _U32.unpack(raw)[0]
        self._data_offset = fp.tell()
        self._read_index = 0
        self._stream_pos = self._data_offset

    def _detect_livetime_header(self):
        """Detect new (102-byte) vs legacy (38-byte) header."""
        fp = self.__fp
        file_size = os.fstat(fp.fileno()).st_size
        nch = len(self.activeChannel)

        # Header sizes including nEvent.
        old_h = _BASE_HEADER.size + 4
        new_h = _BASE_HEADER.size + 8 * _NCH_MAX + 4

        # Exact detection for an empty file.
        if file_size == new_h:
            return True
        if file_size == old_h:
            return False

        # Capture Mode has fixed-size records, so file size gives a strong test.
        if self.capMode == 0:
            record_size = 8 + 4 + nch * (2 + 2 * self.capLength)

            old_n = self._peek_u32(_BASE_HEADER.size)
            new_n = self._peek_u32(_BASE_HEADER.size + 8 * _NCH_MAX)

            old_ok = (old_h + old_n * record_size == file_size)
            new_ok = (new_h + new_n * record_size == file_size)

            if new_ok and not old_ok:
                return True
            if old_ok and not new_ok:
                return False

        # Event Mode: inspect the first event-header magic.
        # If nEvent==0, header size already handled above.
        old_n = self._peek_u32(_BASE_HEADER.size)
        new_n = self._peek_u32(_BASE_HEADER.size + 8 * _NCH_MAX)

        old_magic = self._event_magic_ok(old_h) if old_n else False
        new_magic = self._event_magic_ok(new_h) if new_n else False

        if new_magic and not old_magic:
            return True
        if old_magic and not new_magic:
            return False

        # Current DAQ format is preferred if both are plausible.
        if new_magic:
            return True

        raise CZFormatError(
            "could not determine whether the file contains liveRate[8]; "
            "open with has_livetime=True or False explicitly"
        )

    def _peek_u32(self, offset):
        fp = self.__fp
        here = fp.tell()
        try:
            fp.seek(offset)
            b = fp.read(4)
            return _U32.unpack(b)[0] if len(b) == 4 else 0
        finally:
            fp.seek(here)

    def _event_magic_ok(self, offset):
        fp = self.__fp
        here = fp.tell()
        try:
            fp.seek(offset)
            b = fp.read(2)
            if len(b) != 2:
                return False
            h = _U16.unpack(b)[0]
            return (h & 0xFF0F) == 0x0001
        finally:
            fp.seek(here)

    def print_header(self):
        print("CZData Reader Version:", __version__)
        print("Time Stamp:",
              datetime.datetime.fromtimestamp(self.timestamp))
        print("FPGA Time (unix):",
              datetime.datetime.fromtimestamp(self.time))
        print("FPGA Time (10ns):", self.timeFPGA)
        print("Mode:", self.capMode)
        print("Active Channel:", self.activeChannel)
        print("Length:", self.capLength)
        print("Capture Time (ms):", self.capTime)
        print("Live Rate : ", [f"{rate:.5f}" for rate in self.liveRate],)
        print("NEvent:", self.nEvent)

    def livetime(self, ch=None):
        """Return live fraction.

        ch=None -> 8-element array for physical channels 1..8.
        ch=1..8 -> scalar live fraction for that physical channel.
        """
        if ch is None:
            return self.liveRate
        if ch < 1 or ch > _NCH_MAX:
            raise ValueError("channel must be in 1..8")
        return float(self.liveRate[ch - 1])

    @property
    def activeLiveRate(self):
        if not self.activeChannel:
            return np.empty(0, dtype=np.float64)
        idx = np.asarray(self.activeChannel, dtype=np.intp) - 1
        return self.liveRate[idx]

    # ------------------------------------------------------------------
    # Streaming mode: read one event only
    # ------------------------------------------------------------------
    def rewind(self):
        if not self.isOpen:
            raise ValueError("file is closed")
        self.__fp.seek(self._data_offset)
        self._stream_pos = self._data_offset
        self._read_index = 0

    def read(self):
        """Read one event, keeping only that event in memory.

        Returns None after the number of events stored in the header has been
        reached.  Waveforms are NumPy uint16 arrays.
        """
        if not self.isOpen:
            raise ValueError("file is closed")
        if self._read_index >= self.nEvent:
            return None

        if self._mmap is not None:
            if self.capMode == 0:
                ev = self._read_one_capture_mmap()
            elif self.capMode == 1:
                ev = self._read_one_event_mmap()
            else:
                raise NotImplementedError(
                    f"capture mode {self.capMode} is not implemented"
                )
        else:
            if self.capMode == 0:
                ev = self._read_one_capture()
            elif self.capMode == 1:
                ev = self._read_one_event()
            else:
                raise NotImplementedError(
                    f"capture mode {self.capMode} is not implemented"
                )

        self._read_index += 1
        return ev

    @staticmethod
    def _new_event(time, length, ch, ped, wf, active_channels, header=None):
        # Avoid keyword-heavy __init__ in the hot per-event read() path.
        ev = CZEvent.__new__(CZEvent)
        ev.time = int(time)
        ev.length = int(length)
        ev.ch = ch
        ev.ped = ped
        ev.wf = wf
        ev.header = header
        ev._active_channels = tuple(active_channels)
        return ev

    def _read_one_capture_mmap(self):
        nch = len(self.activeChannel)
        length = int(self.capLength)
        record_size = 12 + nch * (2 + 2 * length)
        pos = self._stream_pos
        end_pos = pos + record_size
        mm = self._mmap
        if end_pos > len(mm):
            raise EOFError("unexpected EOF in Capture Mode record")

        # Copy exactly one record.  The whole file is not copied into RAM.
        b = mm[pos:end_pos]
        self._stream_pos = end_pos
        mv = memoryview(b)

        time = _U64.unpack_from(mv, 0)[0]
        event_length = _U32.unpack_from(mv, 8)[0]
        if event_length != length:
            raise CZFormatError(
                f"capture length mismatch: header={length}, "
                f"event={event_length}"
            )

        ped = [0] * nch
        wf = [None] * nch
        p = 12
        for ich in range(nch):
            ped[ich] = _U16.unpack_from(mv, p)[0]
            p += 2
            wf[ich] = mv[p:p + 2 * length].cast("H")
            p += 2 * length

        return self._new_event(
            time, event_length, list(self.activeChannel), ped, wf,
            self.activeChannel
        )

    def _read_one_event_mmap(self):
        mm = self._mmap
        pos = self._stream_pos
        if pos + 24 > len(mm):
            raise EOFError("unexpected EOF in Event Mode header")

        h0, length, t0, t1, t2 = _EVENT_HEAD5.unpack_from(mm, pos)
        if (h0 & 0xFF0F) != 0x0001:
            raise CZFormatError(
                f"corrupted event header at event {self._read_index}: "
                f"0x{h0:04x}"
            )

        ch = ((h0 & 0x01F0) >> 4) + 1
        try:
            ich = self._active_index[ch]
        except KeyError:
            raise CZFormatError(
                f"event channel {ch} is not in active channel mask "
                f"0x{self.channelMask:08x}"
            ) from None

        time_abs = (int(t2) << 32) | (int(t1) << 16) | int(t0)
        time = time_abs - self.timeFPGA
        pedestal = _U16.unpack_from(mm, pos + 16)[0]

        length_read = (int(length) + 3) & ~3
        payload_start = pos + 24
        record_end = payload_start + 2 * length_read
        if record_end > len(mm):
            raise EOFError("unexpected EOF in Event Mode waveform")

        # Copy only the meaningful waveform of this event.
        payload = mm[payload_start:payload_start + 2 * int(length)]
        waveform = memoryview(payload).cast("H")
        self._stream_pos = record_end

        nch = len(self.activeChannel)
        ped = [0] * nch
        wf = [()] * nch
        ped[ich] = pedestal
        wf[ich] = waveform

        return self._new_event(
            time, length, ch, ped, wf, self.activeChannel
        )

    def _read_exact(self, n):
        if self._mmap is not None:
            p = self._stream_pos
            q = p + n
            if q > len(self._mmap):
                got = max(0, len(self._mmap) - p)
                raise EOFError(
                    f"unexpected EOF: requested {n} bytes, got {got}"
                )
            # bytes slice: copy only the current chunk/event, not the file.
            b = self._mmap[p:q]
            self._stream_pos = q
            return b

        b = self.__fp.read(n)
        if len(b) != n:
            raise EOFError(
                f"unexpected EOF: requested {n} bytes, got {len(b)}"
            )
        return b

    def _read_one_capture(self):
        nch = len(self.activeChannel)
        record_size = 8 + 4 + nch * (2 + 2 * self.capLength)
        b = self._read_exact(record_size)
        mv = memoryview(b)

        time = _U64.unpack_from(mv, 0)[0]
        length = _U32.unpack_from(mv, 8)[0]
        if length != self.capLength:
            raise CZFormatError(
                f"capture length mismatch: header={self.capLength}, "
                f"event={length}"
            )

        ped = [0] * nch
        wf = [None] * nch
        p = 12
        for ich in range(nch):
            ped[ich] = _U16.unpack_from(mv, p)[0]
            p += 2
            wf_bytes = mv[p:p + 2 * length]
            wf[ich] = wf_bytes.cast("H")
            p += 2 * length

        return CZEvent(
            time=time, length=length, ch=list(self.activeChannel),
            ped=ped, wf=wf, active_channels=self.activeChannel
        )

    def _read_one_event(self):
        head = self._read_exact(24)
        h0, length, t0, t1, t2 = _EVENT_HEAD5.unpack_from(head, 0)

        if (h0 & 0xFF0F) != 0x0001:
            raise CZFormatError(
                f"corrupted event header at event {self._read_index}: "
                f"0x{h0:04x}"
            )

        ch = ((h0 & 0x01F0) >> 4) + 1
        if ch not in self._active_index:
            raise CZFormatError(
                f"event channel {ch} is not in active channel mask "
                f"0x{self.channelMask:08x}"
            )

        time_abs = (int(t2) << 32) | (int(t1) << 16) | int(t0)
        time = time_abs - self.timeFPGA
        pedestal = _U16.unpack_from(head, 16)[0]

        length_read = (int(length) + 3) & ~3
        payload = self._read_exact(2 * length_read)
        waveform = memoryview(payload)[:2 * length].cast("H")

        nch = len(self.activeChannel)
        ped = [0] * nch
        wf = [np.empty(0, dtype=np.uint16) for _ in range(nch)]
        ich = self._active_index[ch]
        ped[ich] = pedestal
        wf[ich] = waveform

        return CZEvent(
            time=time, length=length, ch=ch,
            ped=ped, wf=wf,
            header=memoryview(head).cast("H"),
            active_channels=self.activeChannel
        )

    def read_append(self):
        ev = self.read()
        if ev is not None:
            if not isinstance(self.events, list):
                self.events = list(self.events)
            self.events.append(ev)
        return ev

    # ------------------------------------------------------------------
    # Bulk mode: read all data efficiently
    # ------------------------------------------------------------------
    def read_all(self):
        """Read/index all events efficiently.

        Capture Mode
        ------------
        Creates zero-copy NumPy views:
            times       : (N,)
            lengths     : (N,)
            pedestals   : (N, Nch)
            waveforms   : (N, Nch, capLength)

        Active-channel index 0..Nch-1 corresponds to ``activeChannel``.
        For convenience, ``pedestalsByChannel`` and ``waveformsByChannel``
        also provide dictionaries indexed by physical channel number.

        Event Mode
        ----------
        Creates:
            times       : (N,) relative FPGA ticks (10 ns)
            lengths     : (N,)
            channels    : (N,) physical channel numbers
            pedestals   : (N,)
            waveforms   : (N, L) zero-copy ndarray when all events have the
                          same length, otherwise a lazy Sequence of 1D arrays.

        ``events`` becomes a lazy Sequence.  ``events[i]`` still returns a
        CZEvent, but no Python CZEvent objects are pre-created.
        """
        if not self.isOpen:
            raise ValueError("file is closed")

        # Do not pass the already-buffered Python file object to np.fromfile().
        # On Windows, BufferedReader's logical position can differ from the
        # underlying OS file-descriptor position after header parsing/read-ahead.
        # That can make np.fromfile(self.__fp, ...) start at the wrong byte.
        # Reading by filename with an explicit byte offset is deterministic on
        # all supported platforms and remains a single fast C-level bulk read.
        self._bulk_buffer = np.fromfile(
            self.fileName, dtype=np.uint8, offset=self._data_offset
        )

        if self.capMode == 0:
            self._read_all_capture()
        elif self.capMode == 1:
            self._read_all_event()
        else:
            raise NotImplementedError(
                f"capture mode {self.capMode} is not implemented"
            )

        self.events = _BulkEventSequence(self)
        self._read_index = self.nEventRead
        return self

    def _read_all_capture(self):
        nch = len(self.activeChannel)
        length = int(self.capLength)
        record_size = 12 + nch * (2 + 2 * length)
        expected = int(self.nEvent) * record_size

        if self._bulk_buffer.nbytes < expected:
            raise CZFormatError(
                f"truncated capture data: expected {expected} bytes, "
                f"got {self._bulk_buffer.nbytes}"
            )

        # Ignore bytes beyond nEvent records, if any.
        buf = self._bulk_buffer
        n = int(self.nEvent)
        self.nEventRead = n
        self._record_size = record_size

        self.times = np.ndarray(
            (n,), dtype="<u8", buffer=buf, offset=0,
            strides=(record_size,)
        )
        self.lengths = np.ndarray(
            (n,), dtype="<u4", buffer=buf, offset=8,
            strides=(record_size,)
        )
        self.channels = None

        # One Capture-Mode record is
        #   [time:u8][length:u4]
        #   repeated for active channels:
        #       [ped:u2][waveform:u2 * capLength]
        # Therefore channels are regularly strided and can be exposed as
        # one zero-copy 2-D/3-D ndarray.
        channel_stride = 2 + 2 * length
        self.pedestals = np.ndarray(
            (n, nch), dtype="<u2", buffer=buf, offset=12,
            strides=(record_size, channel_stride)
        )
        self.waveforms = np.ndarray(
            (n, nch, length), dtype="<u2", buffer=buf, offset=14,
            strides=(record_size, channel_stride, 2)
        )

        # Physical-channel convenience views.
        self.pedestalsByChannel = {
            ch: self.pedestals[:, ich]
            for ich, ch in enumerate(self.activeChannel)
        }
        self.waveformsByChannel = {
            ch: self.waveforms[:, ich, :]
            for ich, ch in enumerate(self.activeChannel)
        }

    def _read_all_event(self):
        buf = self._bulk_buffer
        mv = memoryview(buf)
        total = buf.nbytes
        n_target = int(self.nEvent)

        times = np.empty(n_target, dtype=np.uint64)
        lengths = np.empty(n_target, dtype=np.uint16)
        channels = np.empty(n_target, dtype=np.uint8)
        pedestals = np.empty(n_target, dtype=np.uint16)
        wave_offset = np.empty(n_target, dtype=np.int64)

        pos = 0
        n = 0
        while n < n_target:
            if pos + 24 > total:
                raise CZFormatError(
                    f"truncated event header: event={n}, offset={pos}"
                )

            h0, length, t0, t1, t2 = _EVENT_HEAD5.unpack_from(mv, pos)
            if (h0 & 0xFF0F) != 0x0001:
                raise CZFormatError(
                    f"corrupted event header: event={n}, "
                    f"offset={pos}, hdr=0x{h0:04x}"
                )

            ch = ((h0 & 0x01F0) >> 4) + 1
            if ch not in self._active_index:
                raise CZFormatError(
                    f"event channel {ch} is not active: event={n}, "
                    f"offset={pos}"
                )

            time_abs = (int(t2) << 32) | (int(t1) << 16) | int(t0)
            times[n] = time_abs - self.timeFPGA
            lengths[n] = length
            channels[n] = ch
            pedestals[n] = _U16.unpack_from(mv, pos + 16)[0]

            payload = pos + 24
            wave_offset[n] = payload
            length_read = (int(length) + 3) & ~3
            pos = payload + 2 * length_read

            if pos > total:
                raise CZFormatError(
                    f"truncated waveform: event={n}, expected end={pos}, "
                    f"file data size={total}"
                )
            n += 1

        self.nEventRead = n
        self.times = times
        self.lengths = lengths
        self.channels = channels
        self.pedestals = pedestals
        self.pedestalsByChannel = None
        self.waveformsByChannel = None
        self._wave_offset = wave_offset

        # Fastest/common case: fixed waveform length.  Records then have a
        # constant stride and all waveforms can be exposed as one zero-copy
        # 2D ndarray.
        if n == 0:
            self.waveforms = np.empty((0, 0), dtype=np.uint16)
            self._record_size = None
        elif np.all(lengths == lengths[0]):
            length = int(lengths[0])
            record_size = 24 + 2 * ((length + 3) & ~3)
            # Validate that all offsets follow the expected constant stride.
            expected_offsets = 24 + np.arange(n, dtype=np.int64) * record_size
            if np.array_equal(wave_offset, expected_offsets):
                self.waveforms = np.ndarray(
                    (n, length), dtype="<u2", buffer=buf,
                    offset=24, strides=(record_size, 2)
                )
                self._record_size = record_size
            else:
                self.waveforms = _VariableWaveformSequence(self)
                self._record_size = None
        else:
            self.waveforms = _VariableWaveformSequence(self)
            self._record_size = None

        if pos < total:
            # Not fatal: a file may contain unused/trailing bytes.
            self.trailingBytes = total - pos
        else:
            self.trailingBytes = 0

    def _bulk_event(self, i):
        if self.capMode == 0:
            ped = [int(x) for x in self.pedestals[i]]
            wf = [self.waveforms[i, ich] for ich in range(len(self.activeChannel))]
            return CZEvent(
                time=int(self.times[i]),
                length=int(self.lengths[i]),
                ch=list(self.activeChannel), ped=ped, wf=wf,
                active_channels=self.activeChannel
            )

        if self.capMode == 1:
            ch = int(self.channels[i])
            ich = self._active_index[ch]
            nch = len(self.activeChannel)
            ped = [0] * nch
            wf = [np.empty(0, dtype=np.uint16) for _ in range(nch)]
            ped[ich] = int(self.pedestals[i])
            wf[ich] = self.waveforms[i]

            # Header is recovered lazily from the bulk buffer.
            head_off = int(self._wave_offset[i]) - 24
            header = np.ndarray(
                (12,), dtype="<u2", buffer=self._bulk_buffer,
                offset=head_off
            )
            return CZEvent(
                time=int(self.times[i]),
                length=int(self.lengths[i]),
                ch=ch, ped=ped, wf=wf, header=header,
                active_channels=self.activeChannel
            )

        raise NotImplementedError

    # ------------------------------------------------------------------
    # Convenience / vectorized analysis
    # ------------------------------------------------------------------
    def pedestal_mean(self, ich):
        """Mean pedestal.

        ``ich`` keeps the old active-channel-index convention in Capture Mode.
        In Event Mode, use an active-channel index; only events belonging to
        that physical channel are included.
        """
        if self.nEventRead == 0:
            # Fallback for old streaming-only usage.
            if isinstance(self.events, list) and self.events:
                vals = [ev.ped[ich] for ev in self.events]
                return float(np.mean(vals))
            return 0.0

        if self.capMode == 0:
            if ich < 0 or ich >= len(self.activeChannel):
                raise IndexError(ich)
            return float(self.pedestals[:, ich].mean())

        if self.capMode == 1:
            ch = self.activeChannel[ich]
            m = (self.channels == ch)
            if not np.any(m):
                return 0.0
            return float(self.pedestals[m].mean())

        return 0.0

    def channel_waveforms(self, ch):
        """Return waveforms for one physical channel after read_all().

        Capture Mode: returns a zero-copy (Nevent, capLength) view.
        Event Mode: returns the records whose event-header channel equals ch.
        """
        if ch not in self._active_index:
            raise ValueError(f"channel {ch} is not active")
        if self.nEventRead == 0:
            raise RuntimeError("call read_all() first")
        if self.capMode == 0:
            return self.waveforms[:, self._active_index[ch], :]
        if self.capMode == 1:
            return self.waveforms[self.channels == ch]
        raise NotImplementedError

    def channel_pedestals(self, ch):
        """Return pedestal values for one physical channel after read_all()."""
        if ch not in self._active_index:
            raise ValueError(f"channel {ch} is not active")
        if self.nEventRead == 0:
            raise RuntimeError("call read_all() first")
        if self.capMode == 0:
            return self.pedestals[:, self._active_index[ch]]
        if self.capMode == 1:
            return self.pedestals[self.channels == ch]
        raise NotImplementedError

    def event_rate(self, ch=None):
        """Recorded event rate [1/s], using capTime from the file header."""
        sec = self.capTime * 1.0e-3
        if sec <= 0:
            return 0.0
        if ch is None or self.capMode != 1 or self.channels is None:
            return self.nEvent / sec
        return float(np.count_nonzero(self.channels == ch) / sec)

    def close(self):
        if self._mmap is not None:
            self._mmap.close()
            self._mmap = None
        if self.isOpen:
            self.__fp.close()
            self.isOpen = False

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()
        return False
