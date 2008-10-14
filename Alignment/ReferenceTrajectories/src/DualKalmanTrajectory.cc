/// \class DualKalmanTrajectory
///
///
///  \author    : Gero Flucke
///  date       : October 2008
///  $Revision$
///  $Date$
///  (last update by $Author$)
 
#include "Alignment/ReferenceTrajectories/interface/DualKalmanTrajectory.h"

#include "TrackingTools/TrajectoryState/interface/TrajectoryStateOnSurface.h"
#include "TrackingTools/PatternTools/interface/Trajectory.h"
#include "TrackingTools/PatternTools/interface/TrajectoryMeasurement.h"

#include "TrackingTools/TrackFitters/interface/TrajectoryStateCombiner.h"
#include "TrackingTools/TransientTrackingRecHit/interface/HelpertRecHit2DLocalPos.h"

#include "DataFormats/CLHEP/interface/AlgebraicObjects.h" 
#include "DataFormats/TrajectoryState/interface/LocalTrajectoryParameters.h"
#include "DataFormats/GeometrySurface/interface/LocalError.h"
#include "DataFormats/GeometryVector/interface/LocalPoint.h"

#include "Alignment/ReferenceTrajectories/interface/ReferenceTrajectory.h"

#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/Utilities/interface/Exception.h"

typedef TransientTrackingRecHit::ConstRecHitContainer ConstRecHitContainer;
typedef TransientTrackingRecHit::ConstRecHitPointer ConstRecHitPointer;

//-----------------------------------------------------------------------------------------------
DualKalmanTrajectory::DualKalmanTrajectory(const Trajectory::DataContainer &trajMeasurements,
					   const TrajectoryStateOnSurface &referenceTsos,
					   const std::vector<unsigned int> &forwardRecHitNums,
					   const std::vector<unsigned int> &backwardRecHitNums,
					   const MagneticField *magField,
					   MaterialEffects materialEffects,
					   PropagationDirection propDir,
					   double mass, int residualMethod)
  : ReferenceTrajectoryBase(referenceTsos.localParameters().mixedFormatVector().kSize,
			    forwardRecHitNums.size() + backwardRecHitNums.size() - 1)
{
  theValidityFlag = this->construct(trajMeasurements, referenceTsos,
				    forwardRecHitNums, backwardRecHitNums,
 				    mass, materialEffects, propDir, magField, residualMethod);
}



//-----------------------------------------------------------------------------------------------
DualKalmanTrajectory::DualKalmanTrajectory(unsigned int nPar, unsigned int nHits)
  : ReferenceTrajectoryBase(nPar, nHits)
{}


//-----------------------------------------------------------------------------------------------
bool DualKalmanTrajectory::construct(const Trajectory::DataContainer &trajMeasurements,
				     const TrajectoryStateOnSurface &refTsos, 
				     const std::vector<unsigned int> &forwardRecHitNums,
				     const std::vector<unsigned int> &backwardRecHitNums,
				     double mass, MaterialEffects materialEffects,
				     const PropagationDirection propDir,
				     const MagneticField *magField, int residualMethod)
{

  ReferenceTrajectoryPtr fwdTraj = this->construct(trajMeasurements, refTsos, forwardRecHitNums,
						     mass, materialEffects,
						     propDir, magField);
  
  ReferenceTrajectoryPtr bwdTraj = this->construct(trajMeasurements, refTsos, backwardRecHitNums,
						     mass, materialEffects,
						     this->oppositeDirection(propDir), magField);

  //   edm::LogError("Alignment") << "@SUB=DualKalmanTrajectory::construct"
  // 			     << "valid fwd/bwd " << fwdTraj->isValid() 
  // 			     << "/" << bwdTraj->isValid();
  
  if (!fwdTraj->isValid() || !bwdTraj->isValid()) {
    return false;
  }

  //
  // Combine both reference trajactories to a dual reference trajectory
  //

//   const std::vector<TrajectoryStateOnSurface>& fwdTsosVec = fwdTraj->trajectoryStates();
//   const std::vector<TrajectoryStateOnSurface>& bwdTsosVec = bwdTraj->trajectoryStates();
//   theTsosVec.insert( theTsosVec.end(), fwdTsosVec.begin(), fwdTsosVec.end() );
//   theTsosVec.insert( theTsosVec.end(), ++bwdTsosVec.begin(), bwdTsosVec.end() );

  // Take hits as they come from the Kalman fit.
  const ConstRecHitContainer &fwdRecHits = fwdTraj->recHits(); 
  const ConstRecHitContainer &bwdRecHits = bwdTraj->recHits();
  theRecHits.insert(theRecHits.end(), fwdRecHits.begin(), fwdRecHits.end());
  theRecHits.insert(theRecHits.end(), ++bwdRecHits.begin(), bwdRecHits.end());

  theParameters = this->extractParameters(refTsos);

  unsigned int nParam   = theParameters.num_row();
  unsigned int nFwdMeas = nMeasPerHit * fwdTraj->numberOfHits();
  unsigned int nBwdMeas = nMeasPerHit * bwdTraj->numberOfHits();

//   theMeasurements.sub( 1, fwdTraj->measurements() );
//   theMeasurements.sub( nFwdMeas+1, bwdTraj->measurements().sub( nMeasPerHit+1, nBwdMeas ) );

//   theMeasurementsCov.sub( 1, fwdTraj->measurementErrors() );
//   theMeasurementsCov.sub( nFwdMeas+1, bwdTraj->measurementErrors().sub( nMeasPerHit+1, nBwdMeas ) );

//   theTrajectoryPositions.sub( 1, fwdTraj->trajectoryPositions() );
//   theTrajectoryPositions.sub( nFwdMeas+1, bwdTraj->trajectoryPositions().sub( nMeasPerHit+1, nBwdMeas ) );

//   theTrajectoryPositionCov.sub( 1, fwdTraj->trajectoryPositionErrors() );
//   theTrajectoryPositionCov.sub( nFwdMeas+1, bwdTraj->trajectoryPositionErrors().sub( nMeasPerHit+1, nBwdMeas ) );

  theDerivatives.sub(1, 1, fwdTraj->derivatives());
  theDerivatives.sub(nFwdMeas+1, 1,
		     bwdTraj->derivatives().sub(nMeasPerHit+1, nBwdMeas, 1, nParam));

  // FIXME: next lines stolen from ReferenceTrajectory, no idea whether OK or not...
  if (refTsos.hasError()) {
    AlgebraicSymMatrix parameterCov = asHepMatrix<5>(refTsos.localError().matrix());
    theTrajectoryPositionCov = parameterCov.similarity(theDerivatives);
  } else {
    theTrajectoryPositionCov = AlgebraicSymMatrix(theDerivatives.num_row(), 1);
  }

  // Fill Kalman part, first for forward, then for backward part.
  return this->fillKalmanPart(trajMeasurements, forwardRecHitNums, true, 0, residualMethod) 
    && this->fillKalmanPart(trajMeasurements, backwardRecHitNums, false,
			    forwardRecHitNums.size(), residualMethod);
}


//-----------------------------------------------------------------------------------------------
ReferenceTrajectory*
DualKalmanTrajectory::construct(const Trajectory::DataContainer &trajMeasurements,
				const TrajectoryStateOnSurface &referenceTsos, 
				const std::vector<unsigned int> &recHitNums,
				double mass, MaterialEffects materialEffects,
				const PropagationDirection propDir,
				const MagneticField *magField) const
{

  ConstRecHitContainer recHits;
  recHits.reserve(recHitNums.size());
  for (unsigned int i = 0; i < recHitNums.size(); ++i) {
    recHits.push_back(trajMeasurements[recHitNums[i]].recHit());
  }

  return new ReferenceTrajectory(referenceTsos, recHits, false, // hits are already ordered
				 magField, materialEffects, propDir, mass);
}

//-----------------------------------------------------------------------------------------------
bool DualKalmanTrajectory::fillKalmanPart(const Trajectory::DataContainer &trajMeasurements,
					  const std::vector<unsigned int> &recHitNums,
					  bool startFirst, unsigned int iNextHit,
					  int residualMethod)
{
  // startFirst==false: skip first hit of recHitNums
  // iNextHit: first hit number to fill data members with
  //
  // Two approaches, choosen by 'residualMethod':
  // 1: Use the unbiased residuals as for residual monitoring.
  // 2: Use the _updated_ state and calculate the sigma that is part of
  //    the pull as sqrt(sigma_hit^2 - sigma_tsos^2).
  //    This should (?) lead to the pull as defined on p. 236 of Blobel's book.
  //    Not sure whether this is 100% applicable/correct here...

  if (residualMethod != 1 && residualMethod != 2) {
    throw cms::Exception("BadConfig")
      << "[DualKalmanTrajectory::fillKalmanPart] expect residualMethod == 1 or 2, not " 
      << residualMethod << ".";
  }

  TrajectoryStateCombiner tsosComb; // needed only for residualMethod==2
  for (unsigned int iMeas = (startFirst ? 0 : 1); iMeas < recHitNums.size(); ++iMeas, ++iNextHit) {
    const TrajectoryMeasurement &trajMeasurement = trajMeasurements[recHitNums[iMeas]];
    TrajectoryStateOnSurface tsos = trajMeasurement.updatedState(); // for method 2
    if (residualMethod == 1) { // overwrite for method 1
      tsos = tsosComb(trajMeasurement.forwardPredictedState(), 
		      trajMeasurement.backwardPredictedState()); 
    }
    if (!tsos.isValid()) return false;
    theTsosVec.push_back(tsos);
    
    if (residualMethod == 1) {
      if (!this->fillMeasurementAndError1(trajMeasurement.recHit(), iNextHit, tsos)) return false;
    } else {
      if (!this->fillMeasurementAndError2(trajMeasurement.recHit(), iNextHit, tsos)) return false;
    }
    this->fillTrajectoryPositions(trajMeasurement.recHit()->projectionMatrix(),
				  tsos, iNextHit);
  }

  return true;
} 

//-----------------------------------------------------------------------------------------------
bool DualKalmanTrajectory::fillMeasurementAndError1(const ConstRecHitPointer &hitPtr,
						    unsigned int iHit,
						    const TrajectoryStateOnSurface &tsos)
{
  // Get the measurements and their errors.
  // We have to add error from hit and tsos. The latter must not be biased from hitPtr!

  // No update of hit with tsos: it comes already from fwd+bwd tsos combination.
  // See also https://hypernews.cern.ch/HyperNews/CMS/get/recoTracking/517/1.html .
  // ConstRecHitPointer newHitPtr(hitPtr->canImproveWithTrack() ? hitPtr->clone(tsos) : hitPtr);
  ConstRecHitPointer newHitPtr(hitPtr);

  const LocalPoint localMeasurement(newHitPtr->localPosition());
//   const LocalError localMeasurementCov(newHitPtr->localPositionError() // sigh! no operator+ on LocalError...
// 				       + tsos.localError().positionError());
  const LocalError hitErr(newHitPtr->localPositionError()); // without APE FIXME: Should we add it?
  const LocalError tsosErr(tsos.localError().positionError());// prediction with APE of other hits
  const LocalError localMeasurementCov(hitErr.xx() + tsosErr.xx(),
				       hitErr.xy() + tsosErr.xy(),
				       hitErr.yy() + tsosErr.yy());

  theMeasurements[nMeasPerHit*iHit]   = localMeasurement.x();
  theMeasurements[nMeasPerHit*iHit+1] = localMeasurement.y();
  theMeasurementsCov[nMeasPerHit*iHit]  [nMeasPerHit*iHit]   = localMeasurementCov.xx();
  theMeasurementsCov[nMeasPerHit*iHit]  [nMeasPerHit*iHit+1] = localMeasurementCov.xy();
  theMeasurementsCov[nMeasPerHit*iHit+1][nMeasPerHit*iHit+1] = localMeasurementCov.yy();

  return false;
}

//-----------------------------------------------------------------------------------------------
bool DualKalmanTrajectory::fillMeasurementAndError2(const ConstRecHitPointer &hitPtr,
						    unsigned int iHit,
						    const TrajectoryStateOnSurface &tsos)
{
  // tsos should be updated state, i.e. track info containing info from hitPtr!

  // No further update of hit: 
  // - The Kalman fit used hitPtr as it comes here (besides APE, see below).
  // - If the hit errors improve, we might get (rare) problems of negative diagonal elements, see below.
  // ConstRecHitPointer newHitPtr(hitPtr->canImproveWithTrack() ? hitPtr->clone(tsos) : hitPtr);
  ConstRecHitPointer newHitPtr(hitPtr);

  const LocalPoint localMeasurement(newHitPtr->localPosition());
  const LocalError hitErrNoAPE(newHitPtr->localPositionError());
  LocalError hitErr;
  if (newHitPtr->det() && newHitPtr->det()->alignmentPositionError()) {
    // We have APE set, but
    // - hit local errors are always without,
    // - the tsos errors include APE since they come from track fit.
    // ==> Add APE manually to avoid that the hit error might be smaller than tsos error.
    const AlgebraicSymMatrix errMat(HelpertRecHit2DLocalPos().parError(hitErrNoAPE,
								       *(newHitPtr->det())));
    hitErr = LocalError(errMat[0][0], errMat[0][1], errMat[1][1]);
//     edm::LogError("Alignment") << "@SUB=DualKalmanTrajectory::fillMeasurementAndError2"
// 			       << "Add APE, was s_x " << sqrt(hitErrNoAPE.xx()) 
// 			       << " s_y " << sqrt(hitErrNoAPE.yy())
// 			       << "\nNow s_x " << sqrt(hitErr.xx())
// 			       << ", s_y " << sqrt(hitErr.yy());
  } else {
    hitErr = hitErrNoAPE;
  }
  const LocalError tsosErr(tsos.localError().positionError());

  // Should not be possible to become negative if all is correct - see above.
  if (hitErr.xx() < tsosErr.xx() || hitErr.yy() < tsosErr.yy()) {
    edm::LogError("Alignment") << "@SUB=DualKalmanTrajectory::fillMeasurementAndError2"
			       << "not OK in subdet " << newHitPtr->geographicalId().subdetId()
			       << "\ns_x " << sqrt(hitErr.xx()) << " " << sqrt(tsosErr.xx())
			       << "\ns_xy " << hitErr.xy() << " " << tsosErr.xy()
			       << "\ns_y " << sqrt(hitErr.yy()) << " " << sqrt(tsosErr.yy());
    return false;
  }

  // cf. Blobel/Lohrmann, p. 236:
  const LocalError localMeasurementCov(hitErr.xx() - tsosErr.xx(), // tsos puts correlation in,
				       hitErr.xy() - tsosErr.xy(), // even for 1D strip!
				       hitErr.yy() - tsosErr.yy());

  theMeasurements[nMeasPerHit*iHit]   = localMeasurement.x();
  theMeasurements[nMeasPerHit*iHit+1] = localMeasurement.y();
  theMeasurementsCov[nMeasPerHit*iHit]  [nMeasPerHit*iHit]   = localMeasurementCov.xx();
  theMeasurementsCov[nMeasPerHit*iHit]  [nMeasPerHit*iHit+1] = localMeasurementCov.xy();
  theMeasurementsCov[nMeasPerHit*iHit+1][nMeasPerHit*iHit+1] = localMeasurementCov.yy();

  return true;
}

//-----------------------------------------------------------------------------------------------
void DualKalmanTrajectory::fillTrajectoryPositions(const AlgebraicMatrix &projection, 
						   const TrajectoryStateOnSurface &tsos, 
						   unsigned int iHit)
{
  // get the local coordinates of the reference trajectory
  // (~copied from ReferenceTrajectory::fillTrajectoryPositions)
  AlgebraicVector mixedLocalParams = asHepVector<5>(tsos.localParameters().mixedFormatVector());
  const AlgebraicVector localPosition(projection * mixedLocalParams);

  theTrajectoryPositions[nMeasPerHit*iHit]   = localPosition[0];
  theTrajectoryPositions[nMeasPerHit*iHit+1] = localPosition[1];
}


//-----------------------------------------------------------------------------------------------
AlgebraicVector
DualKalmanTrajectory::extractParameters(const TrajectoryStateOnSurface &referenceTsos) const
{
  return asHepVector<5>( referenceTsos.localParameters().mixedFormatVector() );
}
