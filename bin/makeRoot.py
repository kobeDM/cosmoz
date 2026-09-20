#!/usr/bin/python3

import czdata
from czdata import CZData
import numpy as np
import sys
import argparse
import ROOT


def main( ):
    parser = argparse.ArgumentParser( )
    # parser.add_argument( "-h", help = "file header", default = "AISTPIX" )
    parser.add_argument( "-p", help = "period number", default = 0 )
    parser.add_argument( "-f", help = "file ID", default = 0 )
    args = parser.parse_args( )

    header = "AISTPIX"

    realtime = np.empty( (1), dtype=np.int32 )
    numEvent = np.empty( (1), dtype=np.int32 )
    numTrig  = np.empty( (1), dtype=np.int32 )
    livetime = np.empty( (1), dtype=np.int32 )
    capLength = np.empty( (1), dtype=np.int32 )
    # pedestal = np.empty( (1), dtype=np.uint32 )

    pername = "per"+str( args.p ).zfill( 4 )
    root_path = f"{pername}/{header}_{args.f}.root"
    root_file = ROOT.TFile( root_path, "RECREATE" )
    tree = ROOT.TTree( "tree", "tree" );
    tree.Branch( "realtime", realtime, "raltime/I" )
    tree.Branch( "numEvent", numEvent, "numEvent/I" )
    tree.Branch( "numTrig",  numTrig, "numTrig/I"  )
    tree.Branch( "livetime", livetime, "livetime/I" )
    tree.Branch( "capLength", capLength, "capLength/I" )
    # tree.Branch( "pedestal", pedestal, "pedestal/D" )
    

    info_path = f"{pername}/{header}_{args.f}.info"
    with open( info_path, "r" ) as info_open:
        for line in info_open:
            if "ElapsedTime:" in line:
                realtime[0] = int(line.split("\t")[1])
            elif "NumEvent:" in line:
                numEvent[0] = int(line.split("\t")[1])
            elif "NumTrig:" in line:
                numTrig[0] = int(line.split("\t")[1])
            elif "LiveTime:" in line:
                livetime[0] = int(line.split("\t")[1])
            elif "CapLength:" in line:
                capLength[0] = int(line.split("\t")[1])
                
    buf1 = np.empty( capLength[0], dtype=np.uint16 )
    buf2 = np.empty( capLength[0], dtype=np.uint16 )
    tree.Branch( "waveform1", buf1, f"waveform1[{capLength[0]}]/s" )
    tree.Branch( "waveform2", buf2, f"waveform2[{capLength[0]}]/s" )

    wave_path = f"{pername}/{header}_{args.f}.wave"
    with CZData( wave_path ) as data:
        # data.print_header()
        while True:
            ev = data.read()
            if ev is None:
                break

            # print(f"time: {ev.time_sec():.8f} s")

            if data.capMode == 0:
                # Capture mode 
                for idx, ch in enumerate( ev.ch ):
                    if ch == 1:
                        buf1[:] = np.asarray(ev.wf[idx],dtype=np.uint16)
                    if ch == 2:
                        buf2[:] = np.asarray(ev.wf[idx],dtype=np.uint16)

                    # pedestal = double(ev.ped[idx])
                tree.Fill( )
                    
            # elif data.capMode == 1:
            #     # Event mode
            #     ich = data.activeChannel.index(ev.ch)
            #     wf = np.asarray(ev.wf[ich])

            #     print("channel: ", ev.ch)
            #     print("pedestal: ", ev.ped[ich])
            #     print("waveform: ", wf)

    tree.Write( )
    root_file.Close( )
    
if __name__ == '__main__':
    main( )
