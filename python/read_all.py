from czdata import CZData
import sys

with CZData(sys.argv[1]) as data:

    data.print_header()

    data.read_all()

    print(data.times)
    print(data.channels)
    print(data.pedestals)
    print(data.waveforms)
