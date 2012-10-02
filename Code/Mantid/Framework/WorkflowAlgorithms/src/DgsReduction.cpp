/*WIKI*

This is the top-level workflow algorithm for direct geometry spectrometer
data reduction. This algorithm is responsible for gathering the necessary
parameters and generating calls to other workflow or standard algorithms.

 *WIKI*/

#include "MantidWorkflowAlgorithms/DgsReduction.h"

#include "MantidAPI/FileProperty.h"
#include "MantidAPI/PropertyManagerDataService.h"
#include "MantidKernel/ArrayProperty.h"
#include "MantidKernel/BoundedValidator.h"
#include "MantidKernel/ConfigService.h"
#include "MantidKernel/FacilityInfo.h"
#include "MantidKernel/ListValidator.h"
#include "MantidKernel/PropertyManager.h"
#include "MantidKernel/PropertyWithValue.h"
#include "MantidKernel/RebinParamsValidator.h"
#include "MantidKernel/System.h"
#include "MantidKernel/VisibleWhenProperty.h"
#include "MantidDataObjects/MaskWorkspace.h"

#include <sstream>

using namespace Mantid::Kernel;
using namespace Mantid::API;
using namespace Mantid::DataObjects;

namespace Mantid
{
  namespace WorkflowAlgorithms
  {

    // Register the algorithm into the AlgorithmFactory
    DECLARE_ALGORITHM(DgsReduction)

    //----------------------------------------------------------------------------------------------
    /** Constructor
     */
    DgsReduction::DgsReduction()
    {
    }

    //----------------------------------------------------------------------------------------------
    /** Destructor
     */
    DgsReduction::~DgsReduction()
    {
    }

    //----------------------------------------------------------------------------------------------
    /// Algorithm's name for identification. @see Algorithm::name
    const std::string DgsReduction::name() const { return "DgsReduction"; };

    /// Algorithm's version for identification. @see Algorithm::version
    int DgsReduction::version() const { return 1; };

    /// Algorithm's category for identification. @see Algorithm::category
    const std::string DgsReduction::category() const { return "Workflow\\Inelastic"; }

    //----------------------------------------------------------------------------------------------
    /// Sets documentation strings for this algorithm
    void DgsReduction::initDocs()
    {
      this->setWikiSummary("Top-level workflow algorithm for DGS reduction.");
      this->setOptionalMessage("Top-level workflow algorithm for DGS reduction.");
    }

    //----------------------------------------------------------------------------------------------
    /** Initialize the algorithm's properties.
     */
    void DgsReduction::init()
    {
      // Sample setup options
      std::string sampleSetup = "Sample Setup";
      this->declareProperty(new FileProperty("SampleInputFile", "",
          FileProperty::OptionalLoad, "_event.nxs"),
          "File containing the sample data to reduce");
      this->declareProperty(new WorkspaceProperty<>("SampleInputWorkspace", "",
          Direction::Input, PropertyMode::Optional),
          "Workspace to be reduced");
      this->declareProperty(new FileProperty("DetCalFilename", "",
          FileProperty::OptionalLoad), "A detector calibration file.");
      this->declareProperty("RelocateDetectors", false,
          "Move detectors to position specified in cal file.");
      auto mustBePositive = boost::make_shared<BoundedValidator<double> >();
      mustBePositive->setLower(0.0);
      this->declareProperty("IncidentEnergyGuess", EMPTY_DBL(), mustBePositive,
          "Set the value of the incident energy guess in meV.");
      this->declareProperty("UseIncidentEnergyGuess", false,
          "Use the incident energy guess as the actual value (will not be calculated).");
      this->declareProperty("TimeZeroGuess", 0.0,
          "Set the value of time zero offset in microseconds.");
      this->setPropertySettings("TimeZeroGuess",
          new VisibleWhenProperty("UseIncidentEnergyGuess", IS_EQUAL_TO, "1"));
      auto mustBePositiveInt = boost::make_shared<BoundedValidator<int> >();
      mustBePositiveInt->setLower(0);
      this->declareProperty("Monitor1SpecId", EMPTY_INT(), mustBePositiveInt,
          "Spectrum ID for the first monitor to use in Ei calculation.");
      this->declareProperty("Monitor2SpecId", EMPTY_INT(), mustBePositiveInt,
          "Spectrum ID for the second monitor to use in Ei calculation.");
      this->declareProperty(new ArrayProperty<double>("EnergyTransferRange",
          boost::make_shared<RebinParamsValidator>(true)),
          "A comma separated list of first bin boundary, width, last bin boundary.\n"
          "Negative width value indicates logarithmic binning.");
      this->declareProperty("SofPhiEIsDistribution", true,
          "The final S(Phi, E) data is made to be a distribution.");
      this->declareProperty("HardMaskFile", "", "A file or workspace containing a hard mask.");
      this->declareProperty("GroupingFile", "", "A file containing grouping (mapping) information.");

      this->setPropertyGroup("SampleInputFile", sampleSetup);
      this->setPropertyGroup("SampleInputWorkspace", sampleSetup);
      this->setPropertyGroup("DetCalFilename", sampleSetup);
      this->setPropertyGroup("RelocateDetectors", sampleSetup);
      this->setPropertyGroup("IncidentEnergyGuess", sampleSetup);
      this->setPropertyGroup("UseIncidentEnergyGuess", sampleSetup);
      this->setPropertyGroup("TimeZeroGuess", sampleSetup);
      this->setPropertyGroup("Monitor1SpecId", sampleSetup);
      this->setPropertyGroup("Monitor2SpecId", sampleSetup);
      this->setPropertyGroup("EnergyTransferRange", sampleSetup);
      this->setPropertyGroup("SofPhiEIsDistribution", sampleSetup);
      this->setPropertyGroup("HardMaskFile", sampleSetup);
      this->setPropertyGroup("GroupingFile", sampleSetup);

      // Data corrections
      std::string dataCorr = "Data Corrections";
      this->declareProperty("FilterBadPulses", false, "If true, filter bad pulses from data.");
      std::vector<std::string> incidentBeamNormOptions;
      incidentBeamNormOptions.push_back("None");
      incidentBeamNormOptions.push_back("ByCurrent");
      incidentBeamNormOptions.push_back("ToMonitor");
      this->declareProperty("IncidentBeamNormalisation", "None",
          boost::make_shared<StringListValidator>(incidentBeamNormOptions),
          "Options for incident beam normalisation on data.");
      this->declareProperty("MonitorIntRangeLow", EMPTY_DBL(),
          "Set the lower bound for monitor integration.");
      this->setPropertySettings("MonitorIntRangeLow",
          new VisibleWhenProperty("IncidentBeamNormalisation", IS_EQUAL_TO, "ToMonitor"));
      this->declareProperty("MonitorIntRangeHigh", EMPTY_DBL(),
          "Set the upper bound for monitor integration.");
      this->setPropertySettings("MonitorIntRangeHigh",
          new VisibleWhenProperty("IncidentBeamNormalisation", IS_EQUAL_TO, "ToMonitor"));
      this->declareProperty("TimeIndepBackgroundSub", false,
          "If true, time-independent background will be calculated and removed.");
      this->declareProperty("TibTofRangeStart", EMPTY_DBL(),
          "Set the lower TOF bound for time-independent background subtraction.");
      this->setPropertySettings("TibTofRangeStart",
          new VisibleWhenProperty("TimeIndepBackgroundSub", IS_EQUAL_TO, "1"));
      this->declareProperty("TibTofRangeEnd", EMPTY_DBL(),
          "Set the upper TOF bound for time-independent background subtraction.");
      this->setPropertySettings("TibTofRangeEnd",
          new VisibleWhenProperty("TimeIndepBackgroundSub", IS_EQUAL_TO, "1"));
      this->declareProperty(new FileProperty("DetectorVanadiumInputFile", "",
          FileProperty::OptionalLoad, "_event.nxs"),
          "File containing the sample detector vanadium data to reduce");
      this->declareProperty(new WorkspaceProperty<>("DetectorVanadiumInputWorkspace", "",
          Direction::Input, PropertyMode::Optional),
          "Sample detector vanadium workspace to be reduced");
      this->declareProperty("SaveProcessedDetVan", false,
          "Save the processed detector vanadium workspace");
      this->declareProperty("UseProcessedDetVan", false, "If true, treat the detector vanadium as processed.\n"
          "This includes not running diagnostics on the processed data.");
      this->declareProperty("UseBoundsForDetVan", false,
          "If true, integrate the detector vanadium over a given range.");
      this->declareProperty("DetVanIntRangeLow", EMPTY_DBL(),
          "Set the lower bound for integrating the detector vanadium.");
      this->setPropertySettings("DetVanIntRangeLow",
          new VisibleWhenProperty("UseBoundsForDetVan", IS_EQUAL_TO, "1"));
      this->declareProperty("DetVanIntRangeHigh", EMPTY_DBL(),
          "Set the upper bound for integrating the detector vanadium.");
      this->setPropertySettings("DetVanIntRangeHigh",
          new VisibleWhenProperty("UseBoundsForDetVan", IS_EQUAL_TO, "1"));
      std::vector<std::string> detvanIntRangeUnits;
      detvanIntRangeUnits.push_back("Energy");
      detvanIntRangeUnits.push_back("Wavelength");
      detvanIntRangeUnits.push_back("TOF");
      this->declareProperty("DetVanIntRangeUnits", "Energy",
          boost::make_shared<StringListValidator>(detvanIntRangeUnits),
          "Options for the units on the detector vanadium integration.");
      this->setPropertySettings("DetVanIntRangeUnits",
          new VisibleWhenProperty("UseBoundsForDetVan", IS_EQUAL_TO, "1"));

      this->setPropertyGroup("FilterBadPulses", dataCorr);
      this->setPropertyGroup("IncidentBeamNormalisation", dataCorr);
      this->setPropertyGroup("MonitorIntRangeLow", dataCorr);
      this->setPropertyGroup("MonitorIntRangeHigh", dataCorr);
      this->setPropertyGroup("TimeIndepBackgroundSub", dataCorr);
      this->setPropertyGroup("TibTofRangeStart", dataCorr);
      this->setPropertyGroup("TibTofRangeEnd", dataCorr);
      this->setPropertyGroup("DetectorVanadiumInputFile", dataCorr);
      this->setPropertyGroup("DetectorVanadiumInputWorkspace", dataCorr);
      this->setPropertyGroup("SaveProcessedDetVan", dataCorr);
      this->setPropertyGroup("UseProcessedDetVan", dataCorr);
      this->setPropertyGroup("UseBoundsForDetVan", dataCorr);
      this->setPropertyGroup("DetVanIntRangeLow", dataCorr);
      this->setPropertyGroup("DetVanIntRangeHigh", dataCorr);
      this->setPropertyGroup("DetVanIntRangeUnits", dataCorr);

      // Finding bad detectors
      std::string findBadDets = "Finding Bad Detectors";
      this->declareProperty("OutputMaskFile", "",
          "The output mask file name used for the results of the detector tests.");
      this->setPropertySettings("OutputMaskFile",
          new VisibleWhenProperty("DetectorVanadiumInputFile", IS_NOT_EQUAL_TO, ""));
      this->declareProperty("HighCounts", 1.0e+10, mustBePositive,
          "Mask detectors above this threshold.");
      this->setPropertySettings("HighCounts",
          new VisibleWhenProperty("DetectorVanadiumInputFile", IS_NOT_EQUAL_TO, ""));
      this->declareProperty("LowCounts", 1.e-10, mustBePositive,
          "Mask detectors below this threshold.");
      this->setPropertySettings("LowCounts",
          new VisibleWhenProperty("DetectorVanadiumInputFile", IS_NOT_EQUAL_TO, ""));
      this->declareProperty("LowOutlier", 0.01,
          "Lower bound defining outliers as fraction of median value");
      this->setPropertySettings("LowOutlier",
          new VisibleWhenProperty("DetectorVanadiumInputFile", IS_NOT_EQUAL_TO, ""));
      this->declareProperty("HighOutlier", 100.,
          "Upper bound defining outliers as fraction of median value");
      this->setPropertySettings("HighOutlier",
          new VisibleWhenProperty("DetectorVanadiumInputFile", IS_NOT_EQUAL_TO, ""));
      this->declareProperty("MedianTestHigh", 2.0, mustBePositive,
          "Mask detectors above this threshold.");
      this->setPropertySettings("MedianTestHigh",
          new VisibleWhenProperty("DetectorVanadiumInputFile", IS_NOT_EQUAL_TO, ""));
      this->declareProperty("MedianTestLow", 0.1, mustBePositive,
          "Mask detectors below this threshold.");
      this->setPropertySettings("MedianTestLow",
          new VisibleWhenProperty("DetectorVanadiumInputFile", IS_NOT_EQUAL_TO, ""));
      this->declareProperty("ErrorBarCriterion", 0.0, mustBePositive,
          "Some selection criteria for the detector tests.");
      this->setPropertySettings("ErrorBarCriterion",
          new VisibleWhenProperty("DetectorVanadiumInputFile", IS_NOT_EQUAL_TO, ""));
      this->declareProperty(new FileProperty("DetectorVanadium2InputFile", "",
          FileProperty::OptionalLoad, "_event.nxs"),
          "File containing detector vanadium data to compare against");
      this->declareProperty(new WorkspaceProperty<>("DetectorVanadium2InputWorkspace", "",
          Direction::Input, PropertyMode::Optional),
          "Detector vanadium workspace to compare against");
      this->declareProperty("DetVanRatioVariation", 1.1, mustBePositive,
          "Mask detectors if the time variation is above this threshold.");
      this->setPropertySettings("DetVanRatioVariation",
          new VisibleWhenProperty("DetectorVanadium2InputFile", IS_NOT_EQUAL_TO, ""));

      this->declareProperty("BackgroundCheck", false,
          "If true, run a background check on detector vanadium.");
      this->declareProperty("SamBkgMedianTestHigh", 1.5, mustBePositive,
          "Mask detectors above this threshold.");
      this->setPropertySettings("SamBkgMedianTestHigh",
          new VisibleWhenProperty("BackgroundCheck", IS_EQUAL_TO, "1"));
      this->declareProperty("SamBkgMedianTestLow", 0.0, mustBePositive,
          "Mask detectors below this threshold.");
      this->setPropertySettings("SamBkgMedianTestLow",
          new VisibleWhenProperty("BackgroundCheck", IS_EQUAL_TO, "1"));
      this->declareProperty("SamBkgErrorBarCriterion", 3.3, mustBePositive,
          "Some selection criteria for the detector tests.");
      this->setPropertySettings("SamBkgErrorBarCriterion",
          new VisibleWhenProperty("BackgroundCheck", IS_EQUAL_TO, "1"));
      this->declareProperty("BackgroundTofStart", EMPTY_DBL(), mustBePositive,
          "Start TOF for the background check.");
      this->setPropertySettings("BackgroundTofStart",
          new VisibleWhenProperty("BackgroundCheck", IS_EQUAL_TO, "1"));
      this->declareProperty("BackgroundTofEnd", EMPTY_DBL(), mustBePositive,
          "End TOF for the background check.");
      this->setPropertySettings("BackgroundTofEnd",
          new VisibleWhenProperty("BackgroundCheck", IS_EQUAL_TO, "1"));

      this->declareProperty("RejectZeroBackground", false,
          "If true, check the background region for anomolies.");

      this->declareProperty("PsdBleed", false, "If true, perform a PSD bleed test.");
      this->declareProperty("MaxFramerate", 0.01, "The maximum framerate to check.");
      this->setPropertySettings("MaxFramerate",
          new VisibleWhenProperty("PsdBleed", IS_EQUAL_TO, "1"));
      this->declareProperty("IgnoredPixels", 80.0,
          "A list of pixels to ignore in the calculations.");
      this->setPropertySettings("IgnoredPixels",
          new VisibleWhenProperty("PsdBleed", IS_EQUAL_TO, "1"));

      this->setPropertyGroup("OutputMaskFile", findBadDets);
      this->setPropertyGroup("HighCounts", findBadDets);
      this->setPropertyGroup("LowCounts", findBadDets);
      this->setPropertyGroup("LowOutlier", findBadDets);
      this->setPropertyGroup("HighOutlier", findBadDets);
      this->setPropertyGroup("MedianTestHigh", findBadDets);
      this->setPropertyGroup("MedianTestLow", findBadDets);
      this->setPropertyGroup("ErrorBarCriterion", findBadDets);
      this->setPropertyGroup("DetectorVanadium2InputFile", findBadDets);
      this->setPropertyGroup("DetectorVanadium2InputWorkspace", findBadDets);
      this->setPropertyGroup("DetVanRatioVariation", findBadDets);
      this->setPropertyGroup("BackgroundCheck", findBadDets);
      this->setPropertyGroup("SamBkgMedianTestHigh", findBadDets);
      this->setPropertyGroup("SamBkgMedianTestLow", findBadDets);
      this->setPropertyGroup("SamBkgErrorBarCriterion", findBadDets);
      this->setPropertyGroup("BackgroundTofStart", findBadDets);
      this->setPropertyGroup("BackgroundTofEnd", findBadDets);
      this->setPropertyGroup("RejectZeroBackground", findBadDets);
      this->setPropertyGroup("PsdBleed", findBadDets);
      this->setPropertyGroup("MaxFramerate", findBadDets);
      this->setPropertyGroup("IgnoredPixels", findBadDets);

      // Absolute units correction
      std::string absUnitsCorr = "Absolute Units Correction";
      this->declareProperty("DoAbsoluteUnits", false,
          "If true, perform an absolute units normalisation.");
      this->declareProperty(new FileProperty("AbsUnitsSampleInputFile", "",
          FileProperty::OptionalLoad),
          "The sample (vanadium) file used in the absolute units normalisation.");
      this->setPropertySettings("AbsUnitsSampleInputFile",
          new VisibleWhenProperty("DoAbsoluteUnits", IS_EQUAL_TO, "1"));
      this->declareProperty(new WorkspaceProperty<>("AbsUnitsSampleInputWorkspace", "",
          Direction::Input, PropertyMode::Optional),
          "The sample (vanadium) workspace for absolute units normalisation.");
      this->setPropertySettings("AbsUnitsSampleInputWorkspace",
          new VisibleWhenProperty("DoAbsoluteUnits", IS_EQUAL_TO, "1"));
      this->declareProperty("AbsUnitsGroupingFile", "",
          "Grouping file for absolute units normalisation.");
      this->setPropertySettings("AbsUnitsGroupingFile",
          new VisibleWhenProperty("DoAbsoluteUnits", IS_EQUAL_TO, "1"));
      this->declareProperty(new FileProperty("AbsUnitsDetectorVanadiumInputFile",
          "", FileProperty::OptionalLoad),
          "The detector vanadium file used in the absolute units normalisation.");
      this->setPropertySettings("AbsUnitsDetectorVanadiumInputFile",
          new VisibleWhenProperty("DoAbsoluteUnits", IS_EQUAL_TO, "1"));
      this->declareProperty(new WorkspaceProperty<>("AbsUnitsDetectorVanadiumInputWorkspace", "",
          Direction::Input, PropertyMode::Optional),
          "The detector vanadium workspace for absolute units normalisation.");
      this->setPropertySettings("AbsUnitsDetectorVanadiumInputWorkspace",
          new VisibleWhenProperty("DoAbsoluteUnits", IS_EQUAL_TO, "1"));
      this->declareProperty("AbsUnitsIncidentEnergy", EMPTY_DBL(), mustBePositive,
          "The incident energy for the vanadium sample.");
      this->setPropertySettings("AbsUnitsIncidentEnergy",
          new VisibleWhenProperty("DoAbsoluteUnits", IS_EQUAL_TO, "1"));
      this->declareProperty("AbsUnitsMinimumEnergy", -1.0,
          "The minimum energy for the integration range.");
      this->setPropertySettings("AbsUnitsMinimumEnergy",
          new VisibleWhenProperty("DoAbsoluteUnits", IS_EQUAL_TO, "1"));
      this->declareProperty("AbsUnitsMaximumEnergy", 1.0,
          "The maximum energy for the integration range.");
      this->setPropertySettings("AbsUnitsMaximumEnergy",
          new VisibleWhenProperty("DoAbsoluteUnits", IS_EQUAL_TO, "1"));
      this->declareProperty("VanadiumMass", 32.58, "The mass of vanadium.");
      this->setPropertySettings("VanadiumMass",
          new VisibleWhenProperty("DoAbsoluteUnits", IS_EQUAL_TO, "1"));
      this->declareProperty("SampleMass", 1.0, "The mass of sample.");
      this->setPropertySettings("SampleMass",
          new VisibleWhenProperty("DoAbsoluteUnits", IS_EQUAL_TO, "1"));
      this->declareProperty("SampleRmm", 1.0, "The rmm of sample.");
      this->setPropertySettings("SampleRmm",
          new VisibleWhenProperty("DoAbsoluteUnits", IS_EQUAL_TO, "1"));

      this->setPropertyGroup("DoAbsoluteUnits", absUnitsCorr);
      this->setPropertyGroup("AbsUnitsSampleInputFile", absUnitsCorr);
      this->setPropertyGroup("AbsUnitsSampleInputWorkspace", absUnitsCorr);
      this->setPropertyGroup("AbsUnitsGroupingFile", absUnitsCorr);
      this->setPropertyGroup("AbsUnitsDetectorVanadiumInputFile", absUnitsCorr);
      this->setPropertyGroup("AbsUnitsDetectorVanadiumInputWorkspace", absUnitsCorr);
      this->setPropertyGroup("AbsUnitsIncidentEnergy", absUnitsCorr);
      this->setPropertyGroup("AbsUnitsMinimumEnergy", absUnitsCorr);
      this->setPropertyGroup("AbsUnitsMaximumEnergy", absUnitsCorr);
      this->setPropertyGroup("VanadiumMass", absUnitsCorr);
      this->setPropertyGroup("SampleMass", absUnitsCorr);
      this->setPropertyGroup("SampleRmm", absUnitsCorr);

      this->declareProperty("ReductionProperties", "__dgs_reduction_properties",
          Direction::Output);
      this->declareProperty(new WorkspaceProperty<>("OutputWorkspace", "",
          Direction::Output), "Provide a name for the output workspace.");
    }

    /**
     * Create a workspace by either loading a file or using an existing
     * workspace.
     */
    Workspace_sptr DgsReduction::loadInputData(const std::string prop,
        const bool mustLoad)
    {
      g_log.warning() << "MustLoad = " << mustLoad << std::endl;
      Workspace_sptr inputWS;

      const std::string inFileProp = prop + "InputFile";
      const std::string inWsProp = prop + "InputWorkspace";

      std::string inputData = this->getPropertyValue(inFileProp);
      const std::string inputWSName = this->getPropertyValue(inWsProp);
      if (!inputWSName.empty() && !inputData.empty())
      {
        if (mustLoad)
        {
          std::ostringstream mess;
          mess << "DgsReduction: Either the " << inFileProp << " property or ";
          mess << inWsProp << " property must be provided, NOT BOTH!";
          throw std::runtime_error(mess.str());
        }
        else
        {
          return boost::shared_ptr<Workspace>();
        }
      }
      else if (!inputWSName.empty())
      {
        inputWS = this->load(inputWSName);
      }
      else if (!inputData.empty())
      {
        const std::string facility = ConfigService::Instance().getFacility().name();
        this->setLoadAlg("Load");
        if ("SNS" == facility)
        {
          std::string monitorFilename = prop + "MonitorFilename";
          this->reductionManager->declareProperty(new PropertyWithValue<std::string>(monitorFilename, inputData));
        }
        // Do ISIS
        else
        {
          std::string detCalFileFromAlg = this->getProperty("DetCalFilename");
          std::string detCalFileProperty = prop + "DetCalFilename";
          std::string detCalFilename("");
          if (!detCalFileFromAlg.empty())
          {
            detCalFilename = detCalFileFromAlg;
          }
          else
          {
            detCalFilename = inputData;
          }
          this->reductionManager->declareProperty(new PropertyWithValue<std::string>(detCalFileProperty, detCalFilename));
        }

        inputWS = this->load(inputData);
      }
      else
      {
        if (mustLoad)
        {
          std::ostringstream mess;
          mess << "DgsReduction: Either the " << inFileProp << " property or ";
          mess << inWsProp << " property must be provided!";
          throw std::runtime_error(mess.str());
        }
        else
        {
          return boost::shared_ptr<Workspace>();
        }
      }

      return inputWS;
    }

    MatrixWorkspace_sptr DgsReduction::loadHardMask()
    {
      const std::string hardMask = this->getProperty("HardMaskFile");
      std::string hardMaskWsName;
      if (hardMask.empty())
      {
        return boost::shared_ptr<MatrixWorkspace>();
      }
      else
      {
        hardMaskWsName = "hard_mask";
        IAlgorithm_sptr loadMask;
        bool castWorkspace = false;
        if (boost::ends_with(hardMask, ".nxs"))
        {
          loadMask = this->createSubAlgorithm("Load");
          loadMask->setProperty("Filename", hardMask);
        }
        else
        {
          const std::string instName = this->reductionManager->getProperty("InstrumentName");
          loadMask = this->createSubAlgorithm("LoadMask");
          loadMask->setProperty("Instrument", instName);
          loadMask->setProperty("InputFile", hardMask);
          castWorkspace = true;
        }
        loadMask->setAlwaysStoreInADS(true);
        loadMask->setProperty("OutputWorkspace", hardMaskWsName);
        loadMask->execute();
        if (castWorkspace)
        {
          MaskWorkspace_sptr tmp = loadMask->getProperty("OutputWorkspace");
          return boost::dynamic_pointer_cast<MatrixWorkspace>(tmp);
        }
        return loadMask->getProperty("OutputWorkspace");
      }
    }

    MatrixWorkspace_sptr DgsReduction::loadGroupingFile(const std::string prop)
    {
      const std::string propName = prop + "GroupingFile";
      const std::string groupFile = this->getProperty(propName);
      std::string groupingWsName;
      if (groupFile.empty())
      {
        return boost::shared_ptr<MatrixWorkspace>();
      }
      else
      {
        try
        {
          groupingWsName = prop + "Grouping";
          IAlgorithm_sptr loadGrpFile = this->createSubAlgorithm("LoadDetectorsGroupingFile");
          loadGrpFile->setAlwaysStoreInADS(true);
          loadGrpFile->setProperty("InputFile", groupFile);
          loadGrpFile->setProperty("OutputWorkspace", groupingWsName);
          loadGrpFile->execute();
          return loadGrpFile->getProperty("OutputWorkspace");
        }
        catch (...)
        {
          // This must be an old format grouping file.
          // Set a property to use later.
          g_log.warning() << "Old format grouping file in use." << std::endl;
          this->reductionManager->declareProperty(new PropertyWithValue<std::string>(
              prop + "OldGroupingFilename", groupFile));
          return boost::shared_ptr<MatrixWorkspace>();
        }
      }
    }

    //----------------------------------------------------------------------------------------------
    /** Execute the algorithm.
     */
    void DgsReduction::exec()
    {
      // Reduction property manager
      const std::string reductionManagerName = this->getProperty("ReductionProperties");
      if (reductionManagerName.empty())
      {
        g_log.error() << "ERROR: Reduction Property Manager name is empty" << std::endl;
        return;
      }
      this->reductionManager = boost::make_shared<PropertyManager>();
      PropertyManagerDataService::Instance().addOrReplace(reductionManagerName,
          this->reductionManager);

      // Put all properties except input files/workspaces into property manager.
      const std::vector<Property *> props = this->getProperties();
      std::vector<Property *>::const_iterator iter = props.begin();
      for (; iter != props.end(); ++iter)
      {
        if (!boost::contains((*iter)->name(), "Input"))
        {
          this->reductionManager->declareProperty((*iter)->clone());
        }
      }
      // Determine the default facility
      const FacilityInfo defaultFacility = ConfigService::Instance().getFacility();

      // Need to load data to get certain bits of information.
      Workspace_sptr sampleWS = this->loadInputData("Sample");
      MatrixWorkspace_sptr WS = boost::dynamic_pointer_cast<MatrixWorkspace>(sampleWS);
      this->reductionManager->declareProperty(new PropertyWithValue<std::string>(
          "InstrumentName", WS->getInstrument()->getName()));

      // Check the facility for the loaded file and make sure it's the
      // same as the default.
      const InstrumentInfo info = ConfigService::Instance().getInstrument(WS->getInstrument()->getName());
      if (defaultFacility.name() != info.facility().name())
      {
        std::ostringstream mess;
        mess << "Default facility must be set to " << defaultFacility.name();
        mess << " in order for reduction to work!";
        throw std::runtime_error(mess.str());
      }

      // Get output workspace pointer
      MatrixWorkspace_sptr outputWS = this->getProperty("OutputWorkspace");

      // Load the hard mask if available
      MatrixWorkspace_sptr hardMaskWS = this->loadHardMask();
      // Load the grouping file if available
      MatrixWorkspace_sptr groupingWS = this->loadGroupingFile("");

      // This will be diagnostic mask if DgsDiagnose is run and hard mask if not.
      MatrixWorkspace_sptr maskWS;

      // Process the sample detector vanadium if present
      Workspace_sptr detVanWS = this->loadInputData("DetectorVanadium", false);
      bool isProcessedDetVan = this->getProperty("UseProcessedDetVan");
      // Process a comparison detector vanadium if present
      Workspace_sptr detVan2WS = this->loadInputData("DetectorVanadium2", false);
      IAlgorithm_sptr detVan;
      Workspace_sptr idetVanWS;
      if (detVanWS && !isProcessedDetVan)
      {
        std::string detVanMaskName = detVanWS->getName() + "_diagmask";

        IAlgorithm_sptr diag = this->createSubAlgorithm("DgsDiagnose");
        diag->setProperty("DetVanWorkspace", detVanWS);
        diag->setProperty("DetVanCompWorkspace", detVan2WS);
        diag->setProperty("SampleWorkspace", sampleWS);
        diag->setProperty("OutputWorkspace", detVanMaskName);
        diag->setProperty("ReductionProperties", reductionManagerName);
        diag->executeAsSubAlg();
        maskWS = diag->getProperty("OutputWorkspace");

        this->declareProperty(new WorkspaceProperty<>("SampleDetVanDiagMask",
            detVanMaskName, Direction::Output));
        this->setProperty("SampleDetVanDiagMask", maskWS);

        detVan = this->createSubAlgorithm("DgsProcessDetectorVanadium");
        detVan->setProperty("InputWorkspace", detVanWS);
        if (!maskWS)
        {
          maskWS = hardMaskWS;
          hardMaskWS.reset();
        }
        detVan->setProperty("MaskWorkspace", maskWS);
        if (groupingWS)
        {
          detVan->setProperty("GroupingWorkspace", groupingWS);
        }
        std::string idetVanName = detVanWS->getName() + "_idetvan";

        detVan->setProperty("OutputWorkspace", idetVanName);
        detVan->setProperty("ReductionProperties", reductionManagerName);
        detVan->executeAsSubAlg();
        MatrixWorkspace_sptr oWS = detVan->getProperty("OutputWorkspace");
        idetVanWS = boost::dynamic_pointer_cast<Workspace>(oWS);
        this->declareProperty(new WorkspaceProperty<>("IntegratedNormWorkspace",
            idetVanName, Direction::Output));
        this->setProperty("IntegratedNormWorkspace", idetVanWS);
      }
      else
      {
        idetVanWS = detVanWS;
        maskWS = boost::dynamic_pointer_cast<MatrixWorkspace>(idetVanWS);
        detVanWS.reset();
      }

      IAlgorithm_sptr etConv = this->createSubAlgorithm("DgsConvertToEnergyTransfer");
      etConv->setProperty("InputWorkspace", sampleWS);
      etConv->setProperty("IntegratedDetectorVanadium", idetVanWS);
      const double ei = this->getProperty("IncidentEnergyGuess");
      etConv->setProperty("IncidentEnergyGuess", ei);
      if (maskWS)
      {
        etConv->setProperty("MaskWorkspace", maskWS);
      }
      if (groupingWS)
      {
        etConv->setProperty("GroupingWorkspace", groupingWS);
      }
      etConv->setProperty("ReductionProperties", reductionManagerName);
      etConv->setProperty("OutputWorkspace", this->getPropertyValue("OutputWorkspace"));
      etConv->executeAsSubAlg();
      outputWS = etConv->getProperty("OutputWorkspace");

      // Perform absolute normalisation if necessary
      Workspace_sptr absSampleWS = this->loadInputData("AbsUnitsSample", false);
      if (absSampleWS)
      {
        std::string absUnitsName = absSampleWS->getName() + "_absunits";
        MatrixWorkspace_sptr absUnitsWS;
        MatrixWorkspace_sptr absGroupingWS = this->loadGroupingFile("AbsUnits");

        // Process absolute units detector vanadium if necessary
        Workspace_sptr absDetVanWS = this->loadInputData("AbsUnitsDetectorVanadium", false);
        Workspace_sptr absIdetVanWS;
        if (absDetVanWS)
        {
          std::string idetVanName = absDetVanWS->getName() + "_idetvan";
          detVan->setProperty("InputWorkspace", absDetVanWS);
          detVan->setProperty("OutputWorkspace", idetVanName);
          if (maskWS)
          {
            detVan->setProperty("MaskWorkspace", maskWS);
          }
          if (absGroupingWS)
          {
            detVan->setProperty("GroupingWorkspace", absGroupingWS);
          }
          detVan->setProperty("AlternateGroupingTag", "AbsUnits");
          detVan->executeAsSubAlg();
          MatrixWorkspace_sptr oWS = detVan->getProperty("OutputWorkspace");
          absIdetVanWS = boost::dynamic_pointer_cast<Workspace>(oWS);
        }
        else
        {
          absIdetVanWS = absDetVanWS;
        }

        const std::string absWsName = absSampleWS->getName() + "_absunits";
        etConv->setProperty("InputWorkspace", absSampleWS);
        etConv->setProperty("OutputWorkspace", absWsName);
        const double ei = this->getProperty("AbsUnitsIncidentEnergy");
        etConv->setProperty("IncidentEnergyGuess", ei);
        etConv->setProperty("IntegratedDetectorVanadium", absIdetVanWS);
        if (maskWS)
        {
          etConv->setProperty("MaskWorkspace", maskWS);
        }
        if (absGroupingWS)
        {
          etConv->setProperty("GroupingWorkspace", absGroupingWS);
        }
        etConv->setProperty("AlternateGroupingTag", "AbsUnits");
        etConv->executeAsSubAlg();
        absUnitsWS = etConv->getProperty("OutputWorkspace");

        const double vanadiumMass = this->getProperty("VanadiumMass");
        const double vanadiumRmm = absUnitsWS->getInstrument()->getNumberParameter("vanadium-rmm")[0];

        absUnitsWS /= (vanadiumMass / vanadiumRmm);

        // Set integration range for absolute units sample
        double eMin = this->getProperty("AbsUnitsMinimumEnergy");
        double eMax = this->getProperty("AbsUnitsMaximumEnergy");
        std::vector<double> params;
        params.push_back(eMin);
        params.push_back(eMax - eMin);
        params.push_back(eMax);

        IAlgorithm_sptr rebin = this->createSubAlgorithm("Rebin");
        rebin->setProperty("InputWorkspace", absUnitsWS);
        rebin->setProperty("OutputWorkspace", absUnitsWS);
        rebin->setProperty("Params", params);
        rebin->executeAsSubAlg();
        absUnitsWS = rebin->getProperty("OutputWorkspace");

        IAlgorithm_sptr cToMWs = this->createSubAlgorithm("ConvertToMatrixWorkspace");
        cToMWs->setProperty("InputWorkspace", absUnitsWS);
        cToMWs->setProperty("OutputWorkspace", absUnitsWS);
        absUnitsWS = cToMWs->getProperty("OutputWorkspace");

        // Run diagnostics
        const double huge = reductionManager->getProperty("HighCounts");
        const double tiny = reductionManager->getProperty("LowCounts");
        const double vanOutLo = absUnitsWS->getInstrument()->getNumberParameter("monovan_lo_bound")[0];
        const double vanOutHi = absUnitsWS->getInstrument()->getNumberParameter("monovan_hi_bound")[0];
        const double vanLo = absUnitsWS->getInstrument()->getNumberParameter("monovan_lo_frac")[0];
        const double vanHi = absUnitsWS->getInstrument()->getNumberParameter("monovan_hi_frac")[0];
        const double vanSigma = absUnitsWS->getInstrument()->getNumberParameter("diag_samp_sig")[0];

        IAlgorithm_sptr diag = this->createSubAlgorithm("DetectorDiagnostic");
        diag->setProperty("InputWorkspace", absUnitsWS);
        diag->setProperty("OutputWorkspace", "absUnitsDiagMask");
        diag->setProperty("LowThreshold", tiny);
        diag->setProperty("HighThreshold", huge);
        diag->setProperty("LowOutlier", vanOutLo);
        diag->setProperty("HighOutlier", vanOutHi);
        diag->setProperty("LowThresholdFraction", vanLo);
        diag->setProperty("HighThresholdFraction", vanHi);
        diag->setProperty("SignificanceTest", vanSigma);
        diag->executeAsSubAlg();
        MatrixWorkspace_sptr absMaskWS = diag->getProperty("OutputWorkspace");

        IAlgorithm_sptr mask = this->createSubAlgorithm("MaskDetectors");
        mask->setProperty("Workspace", absUnitsWS);
        mask->setProperty("MaskedWorkspace", absMaskWS);
        mask->executeAsSubAlg();
        absUnitsWS = mask->getProperty("Workspace");

        IAlgorithm_sptr cFrmDist = this->createSubAlgorithm("ConvertFromDistribution");
        cFrmDist->setProperty("Workspace", absUnitsWS);
        cFrmDist->executeAsSubAlg();
        absUnitsWS = cFrmDist->getProperty("Workspace");

        IAlgorithm_sptr wMean = this->createSubAlgorithm("WeightedMeanOfWorkspace");
        wMean->setProperty("InputWorkspace", absUnitsWS);
        wMean->setProperty("OutputWorkspace", absUnitsWS);
        wMean->executeAsSubAlg();
        absUnitsWS = wMean->getProperty("OutputWorkspace");

        // If the absolute units detector vanadium is used, do extra correction.
        if (!absIdetVanWS)
        {
          Property *prop = absUnitsWS.get()->run().getProperty("Ei");
          const double ei = boost::lexical_cast<double>(prop->value());
          double xsection = 0.0;
          if (200.0 <= ei)
          {
            xsection = 420.0;
          }
          else
          {
            xsection = 400.0 + (ei / 10.0);
          }
          absUnitsWS /= xsection;
          const double sampleMass = this->getProperty("SampleMass");
          const double sampleRmm = this->getProperty("SampleRmm");
          absUnitsWS *= (sampleMass / sampleRmm);
        }

        mask->setProperty("Workspace", outputWS);
        mask->setProperty("MaskedWorkspace", absMaskWS);
        mask->executeAsSubAlg();
        outputWS = mask->getProperty("Workspace");

        // Do absolute normalisation
        outputWS /= absUnitsWS;

        this->declareProperty(new WorkspaceProperty<>("AbsUnitsWorkspace",
            absUnitsName, Direction::Output));
        this->setProperty("AbsUnitsWorkspace", absUnitsWS);
      }

      this->setProperty("OutputWorkspace", outputWS);
    }

  } // namespace Mantid
} // namespace WorkflowAlgorithms
