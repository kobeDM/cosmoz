#!/usr/bin/python3

import os
import sys
import argparse
import subprocess
from subprocess import PIPE
import time
import errno
from tqdm import tqdm
from pathlib import Path

import backupAISTPIX as BKP_AISTPIX
import makeRoot as MAKE_ROOT

TARGET_EXTENSIONS = [ "json", "wave", "info" ]

def symlink_force( source_path, dist_name ):
    try:
        os.symlink( source_path, dist_name )
    except OSError as e:
        if e.errno == errno.EEXIST:
            os.remove( dist_name )
            os.symlink( source_path, dist_name )
        else:
            raise e

    return


def process_period( per_number, header ):

    print( f"Quick look processing period: {per_number}..." )
    print( "" )

    print( f"File transfer start..." )

    current_path = os.getcwd( )
    dataset_name = os.path.basename( current_path )
    data_dir = os.environ["AISTPIX_COSMOZ_DATADIR"]
    per_name = "per" + str( per_number ).zfill( 4 )
    source_path = f"{BKP_AISTPIX.COSMOZ}:/home/cosmoz/data/{dataset_name}/{per_name}"
    target_path = f"{data_dir}/{dataset_name}"
    print( f"source path: {source_path}" )
    print( f"target path: {target_path}" )
    
    BKP_AISTPIX.backup( source_path, target_path )
    print( "" )
    print( f"File tranfer finished." )
    print( f"File link start." )

    if os.path.isdir( per_name ) == False:
        cmd = f"mkdir {per_name}"
        proc = subprocess.run( cmd, shell = True )

    
    files: List[Path] = []
    target_per_path = Path( f"{target_path}/{per_name}" )
    for extension in TARGET_EXTENSIONS:
        for f in target_per_path.glob( f"*.{extension}" ):
            files.append( f )
            
    for file in tqdm( files, desc="linking..." ):
        file_name = os.path.basename( file )
        dist_name = f"./{per_name}/{file_name}"
        symlink_force( file, dist_name )

    print( f"File link finished." )
    print( "" )

    print( f"Decoding start..." )
    print( "" )
    file_number = 0
    while os.path.isfile( f"{per_name}/{header}_{file_number}.wave" ) == True:
        if os.path.isfile( f"{per_name}/{header}_{file_number}.info" ) == True:
            wave_path = f"{per_name}/{header}_{file_number}.wave"
            info_path = f"{per_name}/{header}_{file_number}.info"
            root_path = f"{per_name}/{header}_{file_number}.root"
            print( f"Creating ROOT file: {root_path}" )
            MAKE_ROOT.make_root( wave_path, info_path, root_path )
            file_number += 1

    print( f"Decoding finished." )
    print( "" )
    print( f"Process {per_name} finished!!!" )

    return

def main( ):
    parser = argparse.ArgumentParser( )
    parser.add_argument( "-n", help = "file header", default = "AISTPIX" )
    parser.add_argument( "-f", help = "begining of period number (from)", default = 0 )
    parser.add_argument( "-t", help = "end of period number (to)", default = 0 )
    args = parser.parse_args( )

    header = args.n
    first_period = int( args.f )
    last_period = int( args.t )
    
    if first_period > last_period or last_period <= 0:
        print( f"Error: invalid peiod range..., {first_period} - {last_period}. aborting..." )
        sys.exit( 1 )

    print( "" )
    print( f" === Start Quick Look from period: {first_period} - {last_period} === " )
    print( "" )
    
    for period in range( first_period, last_period + 1 ):
        process_period( period, header )

    return
    
if __name__ == '__main__':
    main( )
