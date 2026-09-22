#!/usr/bin/python3

import czdata
from czdata import CZData
import numpy as np
import os
import sys
import argparse
import ROOT

def make_root( wave_path, info_path, root_path ):

    files = [ wave_path, info_path ]
    if all( os.path.isfile( f ) for f in files ) == False:
        print( "Error: input files are invalid. aborting..." )
        sys.exit( 1 )

    root_file = ROOT.TFile( root_path, "RECREATE" )
    tree = ROOT.TTree( "tree", "tree" );

    realtime  = np.empty( (1), dtype=np.int32 )
    numEvent  = np.empty( (1), dtype=np.int32 )
    numTrig   = np.empty( (1), dtype=np.int32 )
    livetime  = np.empty( (1), dtype=np.int32 )
    capLength = np.empty( (1), dtype=np.int32 )
    pedestal1 = np.empty( (1), dtype=np.int32 )
    pedestal2 = np.empty( (1), dtype=np.int32 )
    frequency = np.empty( (1), dtype=np.int32 )

    tree.Branch( "realtime",  realtime,  "raltime/I"   )
    tree.Branch( "numEvent",  numEvent,  "numEvent/I"  )
    tree.Branch( "numTrig",   numTrig,   "numTrig/I"   )
    tree.Branch( "livetime",  livetime,  "livetime/I"  )
    tree.Branch( "capLength", capLength, "capLength/I" )
    tree.Branch( "frequency", frequency, "frequency/I"  )
    tree.Branch( "pedestal1", pedestal1, "pedestal1/I"  )
    tree.Branch( "pedestal2", pedestal2, "pedestal2/I"  )

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
            elif "SampleFreq:" in line:
                frequency[0] = int(line.split("\t")[1])
                
    buf1 = np.empty( capLength[0], dtype=np.uint16 )
    buf2 = np.empty( capLength[0], dtype=np.uint16 )
    tree.Branch( "waveform1", buf1, f"waveform1[{capLength[0]}]/s" )
    tree.Branch( "waveform2", buf2, f"waveform2[{capLength[0]}]/s" )
    
    with CZData( wave_path ) as data:
        while True:
            ev = data.read()
            if ev is None: break

            if data.capMode == 0: # Capture mode 
                for idx, ch in enumerate( ev.ch ):
                    if ch == 1:
                        buf1[:] = np.asarray(ev.wf[idx],dtype=np.uint16)
                        pedestal1[0] = ev.ped[idx]
                    if ch == 2:
                        buf2[:] = np.asarray(ev.wf[idx],dtype=np.uint16)
                        pedestal2[0] = ev.ped[idx]
                    
                tree.Fill( )

    tree.Write( )
    root_file.Close( )
    
    return

def main( ):
    parser = argparse.ArgumentParser( )
    parser.add_argument( "-n", help = "file header", default = "AISTPIX" )
    parser.add_argument( "-p", help = "period number", default = 0 )
    parser.add_argument( "-f", help = "file ID", default = 0 )
    args = parser.parse_args( )

    header = args.n
    pername = "per"+str( args.p ).zfill( 4 )
    wave_path = f"{pername}/{header}_{args.f}.wave"
    info_path = f"{pername}/{header}_{args.f}.info"
    root_path = f"{pername}/{header}_{args.f}.root"
    make_root( wave_path, info_path, root_path )

    return
    
if __name__ == '__main__':
    main( )
