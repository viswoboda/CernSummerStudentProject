from Gaudi.Configuration import INFO, DEBUG
from k4FWCore import ApplicationMgr, IOSvc
from Configurables import EventDataSvc
from Configurables import DDPandoraPFAIdeaAlgorithm

import os

iosvc = IOSvc()
iosvc.Input = '/eos/user/v/viswobod/CernSummer/Zqq_reco_output/GstarToQQ_ecm91GeV_o2_reco_0.root' #"input_reco_0.root"
iosvc.Output = "output/test.root"#IDEA_o2_v01_5GeVe_pandora.root"

# detector geometry
# if K4GEO is empty, this should use relative path to working directory
from Configurables import GeoSvc

geoservice = GeoSvc("GeoSvc")

#path_to_detector = os.environ.get("K4GEO", "")
#detectors_to_use = [
#    'FCCee/IDEA/compact/IDEA_o1_v03/IDEA_o1_v03.xml'
#]

geoservice.detectors = [
    # "/afs/cern.ch/work/s/sako/private/kfc-dream/k4geo/FCCee/IDEA/compact/IDEA_o1_v03/IDEA_o1_v03.xml"
    # os.path.join(path_to_detector, _det) for _det in detectors_to_use
    #"/u/user/sako/kfc-dream/k4geo/FCCee/IDEA/compact/IDEA_o2_v01/IDEA_o2_v01_CI.xml"
    '/eos/user/v/viswobod/CernSummer/project/k4geo/FCCee/IDEA/compact/IDEA_o2_v01/IDEA_o2_v01.xml'
]


params = {
    "PandoraSettingsXmlFile": "/eos/user/v/viswobod/CernSummer/project/ideaPflow/PandoraSettingsIdea.xml",
    # "inputCaloHitCollection" : "TopoClusterAllCells",
    "inputTrackCollection" : "TracksFromGenParticles",
    # "inputClusterCollection" : "TopoClusterAll",
    "inputClusterCollections" : ["TopoGrownClusters"], # ["CLUEClustersECAL", "CLUEClustersHCAL"],
    "outputPfoCollection" : "PandoraPfaIdea",
    "outputClusterCollection" : "PandoraClusters",
    "IsOption2" : True,
    "CherenkovFieldName" : "cherenkov",
    "inputCaloHitCollections" : [
        "SCEPCal_digi_cheren",
        "SCEPCal_digi_scint",
        "DRBTScin_digi",
        "DRBTCher_digi",
        "DRETScinLeft_digi", #endcaps removed since not in simulation
        "DRETCherLeft_digi",
        "DRETScinRight_digi",
        "DRETCherRight_digi"
    ],
    #"CaloHitCollectionNames" : [
    #    "SCEPCal_digi_scint",
    #    "SCEPCal_digi_cheren",
    #    "DRBTmerged_digi",
    #    "DRETmerged_digi"
    #],
    "CaloSystemIDs" : [
        4,
        5,
        28,
        25
    ],
    "CaloLayerFieldNames" : [
        "depth",
        "depth",
        "",
        ""
    ],
    "CaloCollectionTypes" : [
        "ECAL",
        "ECAL",
        "HCAL",
        "HCAL"
    ],
    "CaloEncodingStrings" : [
        "system:5,phi:7,theta:11,gamma:4,epsilon:4,depth:1,cherenkov:1",
        "system:5,phi:7,theta:11,gamma:4,epsilon:4,depth:1,cherenkov:1",
        "system:5,stave:10,tower:-8,air:6,col:-16,row:16,clad:1,core:1,cherenkov:1",
        "system:5,stave:10,tower:6,air:1,col:16,row:16,clad:1,core:1,cherenkov:1"
    ],
    "CaloCellSizes" : [
        10,
        10,
        2.,
        2.,
    ],
    "OutputLevel" : DEBUG,
}

pandoraIDEA = DDPandoraPFAIdeaAlgorithm("DDPandoraPFAIdeaAlgorithm", **params)

ApplicationMgr(
    TopAlg = [pandoraIDEA],
    EvtSel = "NONE",
    EvtMax = -1,
    ExtSvc = [EventDataSvc("EventDataSvc"),geoservice],
    OutputLevel = INFO,
)
