#!/bin/bash
source /cvmfs/sw-nightlies.hsf.org/key4hep/setup.sh -r  2026-07-28
source /eos/user/v/viswobod/CernSummer/project/k4geo/install/bin/thisk4geo.sh

export LD_LIBRARY_PATH=/eos/user/v/viswobod/CernSummer/project/k4GaudiPandora/install/lib64:$LD_LIBRARY_PATH
export PYTHONPATH=/eos/user/v/viswobod/CernSummer/project/k4GaudiPandora/install/python:$PYTHONPATH
export LD_LIBRARY_PATH=/eos/user/v/viswobod/CernSummer/project/LCContent/install/lib:$LD_LIBRARY_PATH
export LD_LIBRARY_PATH=/eos/user/v/viswobod/CernSummer/project/PandoraSDK/install/lib:$LD_LIBRARY_PATH
export LD_LIBRARY_PATH=/eos/user/v/viswobod/CernSummer/project/PandoraMonitoring/install/lib:$LD_LIBRARY_PATH

export LD_LIBRARY_PATH=/eos/user/v/viswobod/CernSummer/project/k4RecTracker/install/lib64:$LD_LIBRARY_PATH
export PYTHONPATH=/eos/user/v/viswobod/CernSummer/project/k4RecTracker/install/python/:$PYTHONPATH
export LD_LIBRARY_PATH=/eos/user/v/viswobod/CernSummer/project/k4RecCalorimeter/install/lib64:$LD_LIBRARY_PATH
export PYTHONPATH=/eos/user/v/viswobod/CernSummer/project/k4RecCalorimeter/install/python/:$PYTHONPATH