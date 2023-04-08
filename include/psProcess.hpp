#ifndef PS_PROCESS
#define PS_PROCESS

#include <lsAdvect.hpp>
#include <lsDomain.hpp>
#include <lsMesh.hpp>
#include <lsToDiskMesh.hpp>

#include <psAdvectionCallback.hpp>
#include <psDomain.hpp>
#include <psLogger.hpp>
#include <psProcessModel.hpp>
#include <psSmartPointer.hpp>
#include <psSurfaceModel.hpp>
#include <psTranslationField.hpp>
#include <psVelocityField.hpp>

#include <rayBoundCondition.hpp>
#include <rayParticle.hpp>
#include <rayTrace.hpp>

template <typename NumericType, int D> class psProcess {
  using translatorType = std::unordered_map<unsigned long, unsigned long>;
  using psDomainType = psSmartPointer<psDomain<NumericType, D>>;

public:
  template <typename ProcessModelType>
  void setProcessModel(psSmartPointer<ProcessModelType> passedProcessModel) {
    model = std::dynamic_pointer_cast<psProcessModel<NumericType, D>>(
        passedProcessModel);
  }

  void setDomain(psSmartPointer<psDomain<NumericType, D>> passedDomain) {
    domain = passedDomain;
  }

  /// Set the source direction, where the rays should be traced from.
  void setSourceDirection(const rayTraceDirection passedDirection) {
    sourceDirection = passedDirection;
  }

  void setProcessDuration(double passedDuration) {
    processDuration = passedDuration;
  }

  void setNumberOfRaysPerPoint(long numRays) { raysPerPoint = numRays; }

  void setMaxCoverageInitIterations(size_t maxIt) { maxIterations = maxIt; }

  void
  setIntegrationScheme(const lsIntegrationSchemeEnum passedIntegrationScheme) {
    integrationScheme = passedIntegrationScheme;
  }

  // Sets the minimum time between printing intermediate results during the
  // process. If this is set to a non-positive value, no intermediate results
  // are printed.
  void setPrintTimeInterval(const NumericType passedTime) {
    printTime = passedTime;
  }

  void apply() {
    /* ---------- Process Setup --------- */
    if (!model) {
      psLogger::getInstance()
          .addWarning("No process model passed to psProcess.")
          .print();
      return;
    }
    auto name = model->getProcessName();

    if (!domain) {
      psLogger::getInstance()
          .addWarning("No domain passed to psProcess.")
          .print();
      return;
    }

    if (model->getGeometricModel()) {
      model->getGeometricModel()->setDomain(domain);
      psLogger::getInstance().addInfo("Applying geometric model...").print();
      model->getGeometricModel()->apply();
      return;
    }

    if (processDuration == 0.) {
      // apply only advection callback
      if (model->getAdvectionCallback()) {
        model->getAdvectionCallback()->setDomain(domain);
        model->getAdvectionCallback()->applyPreAdvect(0);
      } else {
        psLogger::getInstance()
            .addWarning("No advection callback passed to psProcess.")
            .print();
      }
      return;
    }

    if (!model->getSurfaceModel()) {
      psLogger::getInstance()
          .addWarning("No surface model passed to psProcess.")
          .print();
      return;
    }

    double remainingTime = processDuration;
    assert(domain->getLevelSets()->size() != 0 && "No level sets in domain.");
    const NumericType gridDelta =
        domain->getLevelSets()->back()->getGrid().getGridDelta();

    auto diskMesh = lsSmartPointer<lsMesh<NumericType>>::New();
    auto translator = lsSmartPointer<translatorType>::New();
    lsToDiskMesh<NumericType, D> meshConverter(diskMesh);
    meshConverter.setTranslator(translator);

    auto transField = psSmartPointer<psTranslationField<NumericType>>::New(
        model->getVelocityField()->useTranslationField());
    transField->setTranslator(translator);
    transField->setVelocityField(model->getVelocityField());

    lsAdvect<NumericType, D> advectionKernel;
    advectionKernel.setVelocityField(transField);
    advectionKernel.setIntegrationScheme(integrationScheme);

    for (auto dom : *domain->getLevelSets()) {
      meshConverter.insertNextLevelSet(dom);
      advectionKernel.insertNextLevelSet(dom);
    }

    /* --------- Setup for ray tracing ----------- */
    const bool useRayTracing = model->getParticleTypes() != nullptr;

    rayTraceBoundary rayBoundaryCondition[D];
    rayTrace<NumericType, D> rayTrace;

    if (useRayTracing) {
      // Map the domain boundary to the ray tracing boundaries
      for (unsigned i = 0; i < D; ++i)
        rayBoundaryCondition[i] = convertBoundaryCondition(
            domain->getGrid().getBoundaryConditions(i));

      rayTrace.setSourceDirection(sourceDirection);
      rayTrace.setNumberOfRaysPerPoint(raysPerPoint);
      rayTrace.setBoundaryConditions(rayBoundaryCondition);
      rayTrace.setUseRandomSeeds(useRandomSeeds);
      rayTrace.setCalculateFlux(false);
    }

    // Determine whether advection callback is used
    const bool useAdvectionCallback = model->getAdvectionCallback() != nullptr;
    if (useAdvectionCallback) {
      model->getAdvectionCallback()->setDomain(domain);
    }

    // Determine whether there are process parameters used in ray tracing
    model->getSurfaceModel()->initializeProcessParameters();
    const bool useProcessParams =
        model->getSurfaceModel()->getProcessParameters() != nullptr;

    if (useProcessParams)
      psLogger::getInstance().addInfo("Using process parameters.").print();
    if (useAdvectionCallback)
      psLogger::getInstance().addInfo("Using advection callback.").print();

    bool useCoverages = false;

    // Initialize coverages
    meshConverter.apply();
    auto numPoints = diskMesh->getNodes().size();
    if (!coveragesInitialized)
      model->getSurfaceModel()->initializeCoverages(numPoints);
    if (model->getSurfaceModel()->getCoverages() != nullptr) {
      useCoverages = true;
      psLogger::getInstance().addInfo("Using coverages.").print();
      if (!coveragesInitialized) {
        psLogger::getInstance().addInfo("Initializing coverages ... ").print();
        auto points = diskMesh->getNodes();
        auto normals = *diskMesh->getCellData().getVectorData("Normals");
        auto materialIds =
            *diskMesh->getCellData().getScalarData("MaterialIds");
        rayTrace.setGeometry(points, normals, gridDelta);
        rayTrace.setMaterialIds(materialIds);

        for (size_t iterations = 0; iterations < maxIterations; iterations++) {
          // move coverages to the ray tracer
          rayTracingData<NumericType> rayTraceCoverages =
              movePointDataToRayData(model->getSurfaceModel()->getCoverages());
          if (useProcessParams) {
            // store scalars in addition to coverages
            auto processParams =
                model->getSurfaceModel()->getProcessParameters();
            NumericType numParams = processParams->getScalarData().size();
            rayTraceCoverages.setNumberOfScalarData(numParams);
            for (size_t i = 0; i < numParams; ++i) {
              rayTraceCoverages.setScalarData(
                  i, processParams->getScalarData(i),
                  processParams->getScalarDataLabel(i));
            }
          }
          rayTrace.setGlobalData(rayTraceCoverages);

          auto Rates = psSmartPointer<psPointData<NumericType>>::New();

          for (auto &particle : *model->getParticleTypes()) {
            rayTrace.setParticleType(particle);
            rayTrace.apply();

            // fill up rates vector with rates from this particle type
            auto numRates = particle->getRequiredLocalDataSize();
            auto &localData = rayTrace.getLocalData();
            for (int i = 0; i < numRates; ++i) {
              auto rate = std::move(localData.getVectorData(i));

              // normalize rates
              rayTrace.normalizeFlux(rate);

              Rates->insertNextScalarData(std::move(rate),
                                          localData.getVectorDataLabel(i));
            }
          }

          // move coverages back in the model
          moveRayDataToPointData(model->getSurfaceModel()->getCoverages(),
                                 rayTraceCoverages);
          model->getSurfaceModel()->updateCoverages(Rates);
          coveragesInitialized = true;

          if (psLogger::getVerbosity() >= 3) {
            auto coverages = model->getSurfaceModel()->getCoverages();
            for (size_t idx = 0; idx < coverages->getScalarDataSize(); idx++) {
              auto label = coverages->getScalarDataLabel(idx);
              diskMesh->getCellData().insertNextScalarData(
                  *coverages->getScalarData(idx), label);
            }
            for (size_t idx = 0; idx < Rates->getScalarDataSize(); idx++) {
              auto label = Rates->getScalarDataLabel(idx);
              diskMesh->getCellData().insertNextScalarData(
                  *Rates->getScalarData(idx), label);
            }
            printDiskMesh(diskMesh, name + "_covIinit_" +
                                        std::to_string(iterations) + ".vtp");
            psLogger::getInstance()
                .addInfo("Iteration: " + std::to_string(iterations))
                .print();
          }
        }
      }
    }

    size_t counter = 0;
    while (remainingTime > 0.) {
      psLogger::getInstance()
          .addInfo("Remaining time: " + std::to_string(remainingTime))
          .print();

      auto Rates = psSmartPointer<psPointData<NumericType>>::New();
      meshConverter.apply();
      auto materialIds = *diskMesh->getCellData().getScalarData("MaterialIds");
      auto points = diskMesh->getNodes();

      psUtils::Timer rtTimer;
      rtTimer.start();
      if (useRayTracing) {
        auto normals = *diskMesh->getCellData().getVectorData("Normals");
        rayTrace.setGeometry(points, normals, gridDelta);
        rayTrace.setMaterialIds(materialIds);

        // move coverages to ray tracer
        rayTracingData<NumericType> rayTraceCoverages;
        if (useCoverages) {
          rayTraceCoverages =
              movePointDataToRayData(model->getSurfaceModel()->getCoverages());
          if (useProcessParams) {
            // store scalars in addition to coverages
            auto processParams =
                model->getSurfaceModel()->getProcessParameters();
            NumericType numParams = processParams->getScalarData().size();
            rayTraceCoverages.setNumberOfScalarData(numParams);
            for (size_t i = 0; i < numParams; ++i) {
              rayTraceCoverages.setScalarData(
                  i, processParams->getScalarData(i),
                  processParams->getScalarDataLabel(i));
            }
          }
          rayTrace.setGlobalData(rayTraceCoverages);
        }

        for (auto &particle : *model->getParticleTypes()) {
          rayTrace.setParticleType(particle);
          rayTrace.apply();

          // fill up rates vector with rates from this particle type
          auto numRates = particle->getRequiredLocalDataSize();
          auto &localData = rayTrace.getLocalData();
          for (int i = 0; i < numRates; ++i) {
            auto rate = std::move(localData.getVectorData(i));

            // normalize rates
            rayTrace.normalizeFlux(rate);
            Rates->insertNextScalarData(std::move(rate),
                                        localData.getVectorDataLabel(i));
          }
        }

        // move coverages back to model
        if (useCoverages)
          moveRayDataToPointData(model->getSurfaceModel()->getCoverages(),
                                 rayTraceCoverages);
      }
      rtTimer.finish();
      psLogger::getInstance()
          .addTiming("Top-Down Flux Calculation", rtTimer)
          .print();

      auto velocitites = model->getSurfaceModel()->calculateVelocities(
          Rates, points, materialIds);
      model->getVelocityField()->setVelocities(velocitites);

      if (psLogger::getVerbosity() >= 3) {
        if (velocitites)
          diskMesh->getCellData().insertNextScalarData(*velocitites,
                                                       "velocities");
        if (useCoverages) {
          auto coverages = model->getSurfaceModel()->getCoverages();
          for (size_t idx = 0; idx < coverages->getScalarDataSize(); idx++) {
            auto label = coverages->getScalarDataLabel(idx);
            diskMesh->getCellData().insertNextScalarData(
                *coverages->getScalarData(idx), label);
          }
        }
        for (size_t idx = 0; idx < Rates->getScalarDataSize(); idx++) {
          auto label = Rates->getScalarDataLabel(idx);
          diskMesh->getCellData().insertNextScalarData(
              *Rates->getScalarData(idx), label);
        }
        if (printTime >= 0. &&
            ((processDuration - remainingTime) - printTime * counter) > -1.) {
          printDiskMesh(diskMesh,
                        name + "_" + std::to_string(counter) + ".vtp");
          if (domain->getUseCellSet()) {
            domain->getCellSet()->writeVTU(name + "_cellSet_" +
                                           std::to_string(counter++) + ".vtu");
          }
        }
      }

      // apply advection callback
      if (useAdvectionCallback) {
        bool continueProcess = model->getAdvectionCallback()->applyPreAdvect(
            processDuration - remainingTime);
        if (!continueProcess) {
          psLogger::getInstance()
              .addInfo("Process stopped early by AdvectionCallback during "
                       "`preAdvect`.")
              .print();
          break;
        }
      }

      // move coverages to LS, so they get are moved with the advection step
      if (useCoverages)
        moveCoveragesToTopLS(translator,
                             model->getSurfaceModel()->getCoverages());
      advectionKernel.apply();

      // update the translator to retrieve the correct coverages from the LS
      meshConverter.apply();
      if (useCoverages)
        updateCoveragesFromAdvectedSurface(
            translator, model->getSurfaceModel()->getCoverages());

      // apply advection callback
      if (useAdvectionCallback) {
        if (domain->getUseCellSet()) {
          if (domain->getCellSet()->getCellSetPosition()) {
            domain->getCellSet()->updateMaterials();
          } else {
            domain->getCellSet()->updateSurface();
          }
        }
        bool continueProcess = model->getAdvectionCallback()->applyPostAdvect(
            advectionKernel.getAdvectedTime());
        if (!continueProcess) {
          psLogger::getInstance()
              .addInfo("Process stopped early by AdvectionCallback during "
                       "`postAdvect`.")
              .print();
          break;
        }
      }

      remainingTime -= advectionKernel.getAdvectedTime();
    }

    addMaterialIdsToTopLS(translator,
                          diskMesh->getCellData().getScalarData("MaterialIds"));
  }

private:
  void printSurfaceMesh(lsSmartPointer<lsDomain<NumericType, D>> dom,
                        std::string name) {
    auto mesh = lsSmartPointer<lsMesh<NumericType>>::New();
    lsToSurfaceMesh<NumericType, D>(dom, mesh).apply();
    lsVTKWriter<NumericType>(mesh, name).apply();
  }

  void printDiskMesh(lsSmartPointer<lsMesh<NumericType>> mesh,
                     std::string name) {
    lsVTKWriter<NumericType>(mesh, name).apply();
  }

  rayTraceBoundary convertBoundaryCondition(
      lsBoundaryConditionEnum<D> originalBoundaryCondition) {
    switch (originalBoundaryCondition) {
    case lsBoundaryConditionEnum<D>::REFLECTIVE_BOUNDARY:
      return rayTraceBoundary::REFLECTIVE;

    case lsBoundaryConditionEnum<D>::INFINITE_BOUNDARY:
      return rayTraceBoundary::IGNORE;

    case lsBoundaryConditionEnum<D>::PERIODIC_BOUNDARY:
      return rayTraceBoundary::PERIODIC;

    case lsBoundaryConditionEnum<D>::POS_INFINITE_BOUNDARY:
      return rayTraceBoundary::IGNORE;

    case lsBoundaryConditionEnum<D>::NEG_INFINITE_BOUNDARY:
      return rayTraceBoundary::IGNORE;
    }
    return rayTraceBoundary::IGNORE;
  }

  rayTracingData<NumericType>
  movePointDataToRayData(psSmartPointer<psPointData<NumericType>> pointData) {
    rayTracingData<NumericType> rayData;
    const auto numData = pointData->getScalarDataSize();
    rayData.setNumberOfVectorData(numData);
    for (size_t i = 0; i < numData; ++i) {
      auto label = pointData->getScalarDataLabel(i);
      rayData.setVectorData(i, std::move(*pointData->getScalarData(label)),
                            label);
    }

    return std::move(rayData);
  }

  void
  moveRayDataToPointData(psSmartPointer<psPointData<NumericType>> pointData,
                         rayTracingData<NumericType> &rayData) {
    pointData->clear();
    const auto numData = rayData.getVectorData().size();
    for (size_t i = 0; i < numData; ++i)
      pointData->insertNextScalarData(std::move(rayData.getVectorData(i)),
                                      rayData.getVectorDataLabel(i));
  }

  void
  moveCoveragesToTopLS(lsSmartPointer<translatorType> translator,
                       psSmartPointer<psPointData<NumericType>> coverages) {
    auto topLS = domain->getLevelSets()->back();
    for (size_t i = 0; i < coverages->getScalarDataSize(); i++) {
      auto covName = coverages->getScalarDataLabel(i);
      std::vector<NumericType> levelSetData(topLS->getNumberOfPoints(), 0);
      auto cov = coverages->getScalarData(covName);
      for (const auto iter : *translator.get()) {
        levelSetData[iter.first] = cov->at(iter.second);
      }
      if (auto data = topLS->getPointData().getScalarData(covName);
          data != nullptr) {
        *data = std::move(levelSetData);
      } else {
        topLS->getPointData().insertNextScalarData(std::move(levelSetData),
                                                   covName);
      }
    }
  }

  void addMaterialIdsToTopLS(lsSmartPointer<translatorType> translator,
                             std::vector<NumericType> *materialIds) {
    auto topLS = domain->getLevelSets()->back();
    std::vector<NumericType> levelSetData(topLS->getNumberOfPoints(), 0);
    for (const auto iter : *translator.get()) {
      levelSetData[iter.first] = materialIds->at(iter.second);
    }
    topLS->getPointData().insertNextScalarData(std::move(levelSetData),
                                               "Material");
  }

  void updateCoveragesFromAdvectedSurface(
      lsSmartPointer<translatorType> translator,
      psSmartPointer<psPointData<NumericType>> coverages) {
    auto topLS = domain->getLevelSets()->back();
    for (size_t i = 0; i < coverages->getScalarDataSize(); i++) {
      auto covName = coverages->getScalarDataLabel(i);
      auto levelSetData = topLS->getPointData().getScalarData(covName);
      auto covData = coverages->getScalarData(covName);
      covData->resize(translator->size());
      for (const auto it : *translator.get()) {
        covData->at(it.second) = levelSetData->at(it.first);
      }
    }
  }

  psDomainType domain = nullptr;
  psSmartPointer<psProcessModel<NumericType, D>> model = nullptr;
  NumericType processDuration;
  rayTraceDirection sourceDirection =
      D == 3 ? rayTraceDirection::POS_Z : rayTraceDirection::POS_Y;
  lsIntegrationSchemeEnum integrationScheme =
      lsIntegrationSchemeEnum::ENGQUIST_OSHER_1ST_ORDER;
  long raysPerPoint = 1000;
  bool useRandomSeeds = true;
  size_t maxIterations = 20;
  bool coveragesInitialized = false;
  NumericType printTime = 0.;
};

#endif
