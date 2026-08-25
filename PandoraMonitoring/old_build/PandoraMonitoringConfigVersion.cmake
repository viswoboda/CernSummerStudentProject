##############################################################################
# this file is parsed when FIND_PACKAGE is called with version argument
#
# @author Jan Engels, Desy IT
##############################################################################


SET( ${PACKAGE_FIND_NAME}_VERSION_MAJOR 04 )
SET( ${PACKAGE_FIND_NAME}_VERSION_MINOR 00 )
SET( ${PACKAGE_FIND_NAME}_VERSION_PATCH 03 )


INCLUDE( "/afs/cern.ch/user/v/viswobod/CernSummerStud/PandoraPFA/cmakemodules/MacroCheckPackageVersion.cmake" )
CHECK_PACKAGE_VERSION( ${PACKAGE_FIND_NAME} 04.00.03 )

