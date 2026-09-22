#!/usr/bin/python3

import os
import sys
import argparse
import subprocess
from subprocess import PIPE
import time

COSMOZ = "cosmoz@cosmoz"

def backup( source_path, target_path ):
    
    cmd = f"rsync -av {source_path} {target_path}"
    subprocess.run( cmd, shell = True )
    
    return

def main( ):
    parser = argparse.ArgumentParser( )
    parser.add_argument( "-s", help = "source path", default = "" )
    parser.add_argument( "-t", help = "target path", default = "" )
    args = parser.parse_args( )

    if args.s == "" or args.t == "":
        print( "Error: both -s and -t option are required. aborting..." )
        sys.exit( 1 )

    source_path = f"{COSMOZ}:{args.s}"
    target_path = f"{args.t}"

    backup( source_path, target_path )
    
    return
    
if __name__ == '__main__':
    main( )
