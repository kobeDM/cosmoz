#!/usr/bin/python3

import os
import argparse
import subprocess
from subprocess import PIPE
import time

default_config = "params_cap.json"

def check_config( config_path ):
    if os.path.isfile( config_path ):
        print( f"{config_path} exists." )
    else:
        # make config file from skelton file
        CONFIG_SKEL = f"{os.environ[COSMOZPATH]}/config/{default_config}"
        print( f"\tCOSMOZ config slkelton file: {CONFIG_SKEL}" )

        cmd = f"cp {CONFIG_SKEL} {config_path}"
        proc = subprocess.run( cmd, shell=True, stderr=None, check=False, capture_output=False)

    return

def make_new_period( ) -> str:
    per_number = 0
    while os.path.isdir( "per"+str( per_number ).zfill( 4 ) ):
        per_number += 1

    newper = "per" + str( per_number ).zfill( 4 )
    cmd = "mkdir " + newper
    subprocess.run( cmd, shell = True )

    return newper

def daq_run( header, per_name, config_path):
    for fileID in range( 1000 ):
        cmd = f"daq.elf {config_path} {per_name}/{header}_ {fileID}"
        proc = subprocess.run( cmd, shell = True )

    return
    
def daq_abort( header, per_name):
    # delete the last wave file
    file_number = 0
    while os.path.isfile( f"{per_name}/{header}_{file_number}.wave" ) == True:
        if os.path.isfile( f"{per_name}/{header}_{file_number}.info" ) == False:
            cmd = f"rm {per_name}/{header}_{file_number}.wave"
            proc = subprocess.run( cmd, shell = True )
            break
        file_number += 1
        
    return
    
    
def main( ):
    parser = argparse.ArgumentParser( )
    parser.add_argument( "-n", help = "file header", default = "AISTPIX" )
    parser.add_argument( "-c", help = "configuration file", default = default_config )
    args = parser.parse_args( )

    header = args.n
    config_path = args.c
    check_config( config_path )
    
    per_name = make_new_period( )
    cmd = f"cp {config_path} {per_name}"
    proc = subprocess.run( cmd, shell = True )

    try:
        while True:
            daq_run( header, per_name, config_path )

    except KeyboardInterrupt:
        daq_abort( header, per_name )

    return
    
if __name__ == '__main__':
    main( )
