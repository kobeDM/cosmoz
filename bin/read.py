import czdata
from czdata import CZData
import numpy as np
import sys

with CZData(sys.argv[1]) as data:

    data.print_header()

    while True:
        ev = data.read()

        if ev is None:
            break

        print(f"time: {ev.time_sec():.8f} s")

        if data.capMode == 0:
            # Capture mode
            for ich, ch in enumerate(ev.ch):
                wf = np.asarray(ev.wf[ich])

                print("channel: ", ch)
                print("pedestal: ", ev.ped[ich])
                print("waveform: ", wf)

        elif data.capMode == 1:
            # Event mode
            ich = data.activeChannel.index(ev.ch)
            wf = np.asarray(ev.wf[ich])

            print("channel: ", ev.ch)
            print("pedestal: ", ev.ped[ich])
            print("waveform: ", wf)
