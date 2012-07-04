#include "DataFormats/GeometryVector/interface/GlobalVector.h"
#include "DataFormats/GeometryVector/interface/GlobalPoint.h"
#include "FWCore/Framework/interface/ESHandle.h"
#include "FWCore/Framework/interface/EventSetup.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "MagneticField/Records/interface/IdealMagneticFieldRecord.h" 
#include "MagneticField/Engine/interface/MagneticField.h"
#include "Alignment/ReferenceTrajectories/interface/TrajectoryFactoryPlugin.h"
#include "Alignment/ReferenceTrajectories/interface/ReferenceTrajectory.h" 

#include "Alignment/ReferenceTrajectories/interface/TrajectoryFactoryBase.h"

#include "BzeroReferenceTrajectoryFactory.h"

/// A factory that produces instances of class ReferenceTrajectory from a given TrajTrackPairCollection.
/// If |B| = 0 T and configuration parameter UseBzeroIfFieldOff is True,
/// hand-over to the BzeroReferenceTrajectoryFactory.

class ReferenceTrajectoryFactory : public TrajectoryFactoryBase
{
public:
  ReferenceTrajectoryFactory(const edm::ParameterSet &config);
  virtual ~ReferenceTrajectoryFactory();

  /// Produce the reference trajectories.
  virtual const ReferenceTrajectoryCollection trajectories(const edm::EventSetup &setup,
							   const ConstTrajTrackPairCollection &tracks,
							   const reco::BeamSpot &beamSpot) const;

  virtual const ReferenceTrajectoryCollection trajectories(const edm::EventSetup &setup,
							   const ConstTrajTrackPairCollection &tracks,
							   const ExternalPredictionCollection &external,
							   const reco::BeamSpot &beamSpot) const;

  virtual ReferenceTrajectoryFactory* clone() const { return new ReferenceTrajectoryFactory(*this); }

protected:
  ReferenceTrajectoryFactory(const ReferenceTrajectoryFactory &other);
  const TrajectoryFactoryBase *bzeroFactory() const;

  double theMass;
  //  double theMomentumEstimateFieldOff;
  bool   theUseBzeroIfFieldOff;
  // not 'const TrajectoryFactoryBase *': we want a non-const pointer to a const factory:
  mutable const TrajectoryFactoryBase *theBzeroFactory;
};

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

ReferenceTrajectoryFactory::ReferenceTrajectoryFactory( const edm::ParameterSet & config ) :
  TrajectoryFactoryBase( config ),
  theMass(config.getParameter<double>("ParticleMass")),
  //  theMomentumEstimateFieldOff(config.getParameter<double>("MomentumEstimateFieldOff")),
  theUseBzeroIfFieldOff(config.getParameter<bool>("UseBzeroIfFieldOff")),
  theBzeroFactory(0)
{
}

ReferenceTrajectoryFactory::ReferenceTrajectoryFactory(const ReferenceTrajectoryFactory &other) :
  TrajectoryFactoryBase(other),
  theMass(other.theMass),
  theUseBzeroIfFieldOff(other.theUseBzeroIfFieldOff),
  theBzeroFactory(0) // copy data members, but no double pointing to same Bzero factory...
{
}
 
ReferenceTrajectoryFactory::~ReferenceTrajectoryFactory( void )
{
  delete theBzeroFactory;
}


const ReferenceTrajectoryFactory::ReferenceTrajectoryCollection
ReferenceTrajectoryFactory::trajectories(const edm::EventSetup &setup,
					 const ConstTrajTrackPairCollection &tracks,
					 const reco::BeamSpot &beamSpot) const
{
  edm::ESHandle< MagneticField > magneticField;
  setup.get< IdealMagneticFieldRecord >().get( magneticField );
  if (theUseBzeroIfFieldOff && magneticField->inTesla(GlobalPoint(0.,0.,0.)).mag2() < 1.e-6) {
    return this->bzeroFactory()->trajectories(setup, tracks, beamSpot);
  }
  

  ReferenceTrajectoryCollection trajectories;

  ConstTrajTrackPairCollection::const_iterator itTracks = tracks.begin();
  
  while ( itTracks != tracks.end() )
  { 
    TrajectoryInput input = this->innermostStateAndRecHits( *itTracks );
    
    // Check input: If all hits were rejected, the TSOS is initialized as invalid.
    if ( input.first.isValid() )
    {
      // set the flag for reversing the RecHits to false, since they are already in the correct order.
      trajectories.push_back(ReferenceTrajectoryPtr(new ReferenceTrajectory(input.first, input.second, false,
                                                                            magneticField.product(), materialEffects(),
                                                                            propagationDirection(), theMass, 
                                                                            theUseBeamSpot, beamSpot)));
    }

    ++itTracks;
  }

  return trajectories;
}


const ReferenceTrajectoryFactory::ReferenceTrajectoryCollection
ReferenceTrajectoryFactory::trajectories( const edm::EventSetup & setup,
					  const ConstTrajTrackPairCollection& tracks,
					  const ExternalPredictionCollection& external,
					  const reco::BeamSpot &beamSpot) const
{
  ReferenceTrajectoryCollection trajectories;

  if ( tracks.size() != external.size() )
  {
    edm::LogInfo("ReferenceTrajectories") << "@SUB=ReferenceTrajectoryFactory::trajectories"
					  << "Inconsistent input:\n"
					  << "\tnumber of tracks = " << tracks.size()
					  << "\tnumber of external predictions = " << external.size();
    return trajectories;
  }

  edm::ESHandle< MagneticField > magneticField;
  setup.get< IdealMagneticFieldRecord >().get( magneticField );
  if (theUseBzeroIfFieldOff && magneticField->inTesla(GlobalPoint(0.,0.,0.)).mag2() < 1.e-6) {
    return this->bzeroFactory()->trajectories(setup, tracks, external, beamSpot);
  }

  ConstTrajTrackPairCollection::const_iterator itTracks = tracks.begin();
  ExternalPredictionCollection::const_iterator itExternal = external.begin();

  while ( itTracks != tracks.end() )
  {
    TrajectoryInput input = innermostStateAndRecHits( *itTracks );
    // Check input: If all hits were rejected, the TSOS is initialized as invalid.
    if ( input.first.isValid() )
    {
      if ( (*itExternal).isValid() && sameSurface( (*itExternal).surface(), input.first.surface() ) )
      {
        // set the flag for reversing the RecHits to false, since they are already in the correct order.
	ReferenceTrajectoryPtr refTraj( new ReferenceTrajectory( *itExternal, input.second, false,
                                                                 magneticField.product(), materialEffects(),
                                                                 propagationDirection(), theMass,
                                                                 theUseBeamSpot, beamSpot) );
	  
        AlgebraicSymMatrix externalParamErrors( asHepMatrix<5>( (*itExternal).localError().matrix() ) );
        refTraj->setParameterErrors( externalParamErrors );
	trajectories.push_back( refTraj );
      }
      else
      {
        trajectories.push_back(ReferenceTrajectoryPtr(new ReferenceTrajectory(input.first, input.second, false,
                                                                              magneticField.product(), materialEffects(),
                                                                              propagationDirection(), theMass,
                                                                              theUseBeamSpot, beamSpot)));
      }
    }
    
    ++itTracks;
    ++itExternal;
  }
  
  return trajectories;
}

#include "DataFormats/Provenance/interface/ParameterSetID.h" // FIXME: for 51X!

const TrajectoryFactoryBase *ReferenceTrajectoryFactory::bzeroFactory() const
{
  if (!theBzeroFactory) {
    edm::LogInfo("Alignment") << "@SUB=ReferenceTrajectoryFactory::bzeroFactory"
			      << "Using BzeroReferenceTrajectoryFactory for some events.";
    // const edm::ParameterSet &pset = this->configuration();
    // const double momentumEstimate = pset.getParameter<double>("MomentumEstimateFieldOff");
    // theBzeroFactory = new BzeroReferenceTrajectoryFactory(pset, momentumEstimate);
    const edm::ParameterSet &myPset = this->configuration();
    edm::ParameterSet pset;
    // FIXME: not in 51X: pset.copyForModify(myPset); // workaround follows
    pset = myPset;
    pset.setID(edm::ParameterSetID());
    // end workaround
    pset.addParameter("MomentumEstimate", myPset.getParameter<double>("MomentumEstimateFieldOff"));
    theBzeroFactory = new BzeroReferenceTrajectoryFactory(pset);//, momentumEstimate);
  }
  return theBzeroFactory;
}

DEFINE_EDM_PLUGIN( TrajectoryFactoryPlugin, ReferenceTrajectoryFactory, "ReferenceTrajectoryFactory" );
