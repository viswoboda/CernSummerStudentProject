/**
 *  @file   LCContent/src/LCFragmentRemoval/SatelliteAssignmentOnnxAlgorithm.cc
 *
 *  @brief  ONNX cluster-grouping satellite assignment (SPLIT policy).  Token set, feature layout
 *          and pfo-indexing reproduce the model's training tokenisation.
 */
#include "Pandora/AlgorithmHeaders.h"
#include "Api/PandoraContentApi.h"
#include "Objects/Cluster.h"
#include "Objects/CaloHit.h"
#include "Objects/Track.h"

#include "LCHelpers/VectorHelper.h"
#include "LCFragmentRemoval/SatelliteAssignmentOnnxAlgorithm.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <utility>
#include <vector>

using namespace pandora;

namespace lc_content {

namespace {
template <typename T>
inline Ort::Value MakeTensor(std::vector<T> &v, const std::vector<int64_t> &shape, const Ort::MemoryInfo &mem) {
  return Ort::Value::CreateTensor<T>(mem, v.data(), v.size(), shape.data(), shape.size());
}
} // anonymous namespace

//------------------------------------------------------------------------------------------------------------------------------------------

SatelliteAssignmentOnnxAlgorithm::~SatelliteAssignmentOnnxAlgorithm() {
  delete m_pSession;
  delete m_pEnv;
}

void SatelliteAssignmentOnnxAlgorithm::LoadModel() {
  m_pEnv = new Ort::Env(ORT_LOGGING_LEVEL_WARNING, "SatelliteAssignmentOnnx");
  Ort::SessionOptions options;
  options.SetIntraOpNumThreads(1);
  m_pSession = new Ort::Session(*m_pEnv, m_modelPath.c_str(), options);   // throws on a bad path

  Ort::AllocatorWithDefaultOptions allocator;
  for (std::size_t i = 0; i < m_pSession->GetInputCount(); ++i)
    m_inputNames.emplace_back(m_pSession->GetInputNameAllocated(i, allocator).get());
  for (std::size_t i = 0; i < m_pSession->GetOutputCount(); ++i)
    m_outputNames.emplace_back(m_pSession->GetOutputNameAllocated(i, allocator).get());
}

//------------------------------------------------------------------------------------------------------------------------------------------

SatelliteAssignmentOnnxAlgorithm::Hit SatelliteAssignmentOnnxAlgorithm::ExtractHit(const CaloHit *const pHit) {
  Hit hit;
  float r = 0.f, phi = 0.f, theta = 0.f;
  pHit->GetPositionVector().GetSphericalCoordinates(r, phi, theta);   // a calo hit is never at the origin
  hit.theta  = theta;
  hit.phi    = phi;
  hit.energy = pHit->GetInputEnergy();
  hit.isEcal = (pHit->GetHadronicEnergy() <= 0.f);                    // ECAL hits carry no hadronic energy
  hit.isCher = (pHit->GetHitType() == DRC_CHEREN);
  hit.depth  = hit.isEcal ? static_cast<float>(pHit->GetLayer()) : SENT;  // ECAL layer index; SENT for HCAL
  return hit;
}

//------------------------------------------------------------------------------------------------------------------------------------------

bool SatelliteAssignmentOnnxAlgorithm::BuildInfo(const Cluster *const pCluster, ClusterInfo &info) const {
  CaloHitList caloHits;
  pCluster->GetOrderedCaloHitList().FillCaloHitList(caloHits);
  if (caloHits.empty())
    return false;

  double sumX = 0., sumY = 0., sumZ = 0., sumE = 0.;
  for (const CaloHit *const pHit : caloHits) {
    const float energy = pHit->GetInputEnergy();
    if (energy <= 0.f)
      continue;
    info.hits.push_back(ExtractHit(pHit));
    const CartesianVector &position = pHit->GetPositionVector();
    sumX += position.GetX() * energy;
    sumY += position.GetY() * energy;
    sumZ += position.GetZ() * energy;
    sumE += energy;
  }
  if (info.hits.empty() || sumE <= 0.)
    return false;

  const CartesianVector centroid(static_cast<float>(sumX / sumE),
                                 static_cast<float>(sumY / sumE),
                                 static_cast<float>(sumZ / sumE));
  if (centroid.GetMagnitude() <= std::numeric_limits<float>::epsilon())
    return false;

  info.pCluster    = pCluster;
  info.centroidDir = centroid.GetUnitVector();

  // any track reaching the calorimeter makes this a charged primary (track state taken there, no
  // projection; no |p| cut -- matches the training tokenisation); the highest-momentum track is used
  const Track *pBestTrack = nullptr;
  float bestMomentum = 0.f;
  for (const Track *const pTrack : pCluster->GetAssociatedTrackList()) {
    if (!pTrack->ReachesCalorimeter())
      continue;
    const float momentum = pTrack->GetTrackStateAtCalorimeter().GetMomentum().GetMagnitude();
    if (momentum > bestMomentum) {
      bestMomentum = momentum;
      pBestTrack   = pTrack;
    }
  }
  if (pBestTrack) {
    float r = 0.f, phi = 0.f, theta = 0.f;
    pBestTrack->GetTrackStateAtCalorimeter().GetPosition().GetSphericalCoordinates(r, phi, theta);
    info.hasTrack = true;
    info.trkTheta = theta;
    info.trkPhi   = phi;
    info.trkP     = bestMomentum;
  }
  return true;
}

//------------------------------------------------------------------------------------------------------------------------------------------

float SatelliteAssignmentOnnxAlgorithm::BestChargedAffinity(const std::vector<ClusterInfo> &clusters,
                                                            const int candIndex,
                                                            const Cluster *&pBestParent) const {
  pBestParent = nullptr;
  if (!m_pSession)
    return -1.f;
  const ClusterInfo &candidate = clusters[candIndex];

  // anchor reference frame = candidate energy-weighted centroid direction
  float r = 0.f, anchorPhi = 0.f, anchorTheta = 0.f;
  candidate.centroidDir.GetSphericalCoordinates(r, anchorPhi, anchorTheta);

  std::vector<float>   tokenData;
  std::vector<int64_t> typeIds, pfoIds;

  auto addHit = [&](const Hit &hit, const int type, const int pfo) {
    tokenData.push_back(hit.theta - anchorTheta);
    tokenData.push_back(VectorHelper::deltaPhi(hit.phi, anchorPhi));
    tokenData.push_back(hit.depth);
    tokenData.push_back(std::log(std::max(hit.energy, 1e-9f)));
    tokenData.push_back(hit.isCher ? 1.f : 0.f);
    tokenData.push_back(hit.isEcal ? 1.f : 0.f);
    typeIds.push_back(type);
    pfoIds.push_back(pfo);
  };
  auto addTrack = [&](const ClusterInfo &cluster, const int pfo) {
    tokenData.push_back(cluster.trkTheta - anchorTheta);
    tokenData.push_back(VectorHelper::deltaPhi(cluster.trkPhi, anchorPhi));
    tokenData.push_back(SENT);
    tokenData.push_back(std::log(std::max(cluster.trkP, 1e-6f)));
    tokenData.push_back(SENT);
    tokenData.push_back(SENT);
    typeIds.push_back(TYPE_TRACK);
    pfoIds.push_back(pfo);
  };
  auto topByEnergy = [](std::vector<Hit> hits, const int k) {        // copy, keep top-K by energy
    if (static_cast<int>(hits.size()) > k) {
      std::nth_element(hits.begin(), hits.begin() + k, hits.end(),
                       [](const Hit &a, const Hit &b) { return a.energy > b.energy; });
      hits.resize(static_cast<std::size_t>(k));
    }
    return hits;
  };

  // own tokens (pfo 0)
  for (const Hit &hit : topByEnergy(candidate.hits, m_budgetOwn))
    addHit(hit, TYPE_OWN, 0);

  // ALL in-cone neighbours (no cap on the number of neighbour clusters) -> pfo 1..K
  std::vector<std::pair<int, const Cluster *>> trackedPfo;   // (pfo index, candidate parent cluster)
  int pfo = 0;
  for (int j = 0; j < static_cast<int>(clusters.size()); ++j) {
    if (j == candIndex)
      continue;
    if (candidate.centroidDir.GetCosOpeningAngle(clusters[j].centroidDir) <= m_coneCos)
      continue;
    ++pfo;                                                   // 1-based neighbour pfo index
    const ClusterInfo &neighbour = clusters[j];
    for (const Hit &hit : topByEnergy(neighbour.hits, m_budgetNbr))
      addHit(hit, TYPE_OTHER, pfo);
    if (neighbour.hasTrack) {
      addTrack(neighbour, pfo);
      trackedPfo.emplace_back(pfo, neighbour.pCluster);
    }
  }
  const int nNeighbours = pfo;
  if (trackedPfo.empty() || nNeighbours == 0)                // no charged target in cone -> keep neutral
    return -1.f;

  const int64_t nTokens = static_cast<int64_t>(typeIds.size());
  const std::vector<int64_t> tokenShape{1, nTokens, N_FEAT}, idShape{1, nTokens};
  std::vector<Ort::Value> inputs;
  inputs.emplace_back(MakeTensor<float>(tokenData, tokenShape, m_mem));
  inputs.emplace_back(MakeTensor<int64_t>(typeIds, idShape, m_mem));
  inputs.emplace_back(MakeTensor<int64_t>(pfoIds, idShape, m_mem));

  std::vector<const char *> inputNames, outputNames;
  for (const std::string &name : m_inputNames)  inputNames.push_back(name.c_str());
  for (const std::string &name : m_outputNames) outputNames.push_back(name.c_str());

  try {
    auto outputs = m_pSession->Run(Ort::RunOptions{nullptr}, inputNames.data(), inputs.data(),
                                   inputs.size(), outputNames.data(), outputNames.size());
    const float *logits = outputs[0].GetTensorMutableData<float>();   // length = nNeighbours (dynamic)
    float best = -1.f;
    for (const std::pair<int, const Cluster *> &tp : trackedPfo) {
      const int slot = tp.first - 1;                                  // pfo k -> output slot k-1
      if (slot < 0 || slot >= nNeighbours)
        continue;
      const float affinity = 1.f / (1.f + std::exp(-logits[slot]));   // sigmoid
      if (affinity > best) {
        best        = affinity;
        pBestParent = tp.second;
      }
    }
    return best;
  } catch (const Ort::Exception &exception) {
    std::cout << "SatelliteAssignmentOnnx: inference failed (" << exception.what()
              << "); keeping the cluster." << std::endl;
    return -1.f;
  }
}

//------------------------------------------------------------------------------------------------------------------------------------------

pandora::StatusCode SatelliteAssignmentOnnxAlgorithm::Run() {
  if (!m_pSession)
    return STATUS_CODE_FAILURE;

  const ClusterList *pClusterList = nullptr;
  PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::GetCurrentList(*this, pClusterList));

  // ---- per-event cache: every cluster's hits, centroid and (if any) track state (local, no member) ----
  std::vector<ClusterInfo> clusters;
  for (const Cluster *const pCluster : *pClusterList) {
    ClusterInfo info;
    if (this->BuildInfo(pCluster, info))
      clusters.push_back(std::move(info));
  }

  // ---- candidates: available, trackless, non-photon clusters ----
  std::vector<int> candidateIndices;
  for (int i = 0; i < static_cast<int>(clusters.size()); ++i) {
    const Cluster *const pCluster = clusters[i].pCluster;
    if (!pCluster->IsAvailable())
      continue;
    if (clusters[i].hasTrack)                                 // calo-reaching track -> charged primary, not a candidate
      continue;
    if (PHOTON == pCluster->GetParticleId())                  // photons left alone (handled upstream)
      continue;
    candidateIndices.push_back(i);
  }

  // ---- SPLIT decision: merge each candidate into its highest-affinity tracked neighbour if > threshold ----
  std::map<const Cluster *, const Cluster *> mergeMap;          // child -> parent
  for (const int i : candidateIndices) {
    const Cluster *pParent = nullptr;
    const float affinity = this->BestChargedAffinity(clusters, i, pParent);
    if ((affinity > m_affinityThreshold) && pParent && (pParent != clusters[i].pCluster))
      mergeMap[clusters[i].pCluster] = pParent;
  }

  for (const auto &childParent : mergeMap) {
    const Cluster *const pParent = childParent.second;
    if (!pParent || !pParent->IsAvailable())                  // null check first (IsAvailable on null
      continue;                                               // would segfault); a target cannot be stale
                                                              // here -- it is tracked, never a merge child
    PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=,
        PandoraContentApi::MergeAndDeleteClusters(*this, pParent, childParent.first));
  }

  return STATUS_CODE_SUCCESS;
}

//------------------------------------------------------------------------------------------------------------------------------------------

pandora::StatusCode SatelliteAssignmentOnnxAlgorithm::ReadSettings(const pandora::TiXmlHandle xmlHandle) {
  PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=,
      XmlHelper::ReadValue(xmlHandle, "ModelPath", m_modelPath));               // required

  PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=,
      XmlHelper::ReadValue(xmlHandle, "AffinityThreshold", m_affinityThreshold));
  PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=,
      XmlHelper::ReadValue(xmlHandle, "ConeHalfAngle", m_coneHalfAngle));
  PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=,
      XmlHelper::ReadValue(xmlHandle, "BudgetOwn", m_budgetOwn));
  PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=,
      XmlHelper::ReadValue(xmlHandle, "BudgetNeighbour", m_budgetNbr));

  m_coneCos = std::cos(m_coneHalfAngle);
  this->LoadModel();
  return STATUS_CODE_SUCCESS;
}

} // namespace lc_content
