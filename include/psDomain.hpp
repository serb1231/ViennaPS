#ifndef PS_DOMAIN_HPP
#define PS_DOMAIN_HPP

#include <lsBooleanOperation.hpp>
#include <lsDomain.hpp>
#include <lsMakeGeometry.hpp>
#include <lsToDiskMesh.hpp>
#include <lsToSurfaceMesh.hpp>
#include <lsWriter.hpp>

#include <csDenseCellSet.hpp>

#include <psMaterials.hpp>
#include <psSmartPointer.hpp>
#include <psSurfacePointValuesToLevelSet.hpp>
#include <psVTKWriter.hpp>

/**
  This class represents all materials in the simulation domain.
  It contains level sets for the accurate surface representation
  and a cell-based structure for the storage of volume information.
  These structures are used depending on the process applied to the material.
  Processes may use one of either structures or both.
*/
template <class NumericType = float, int D = 3> class psDomain {
public:
  typedef psSmartPointer<lsDomain<NumericType, D>> lsDomainType;
  typedef psSmartPointer<std::vector<lsDomainType>> lsDomainsType;
  typedef psSmartPointer<csDenseCellSet<NumericType, D>> csDomainType;
  typedef psSmartPointer<psMaterialMap> materialMapType;

  static constexpr char materialIdsLabel[] = "MaterialIds";

private:
  lsDomainsType levelSets = nullptr;
  csDomainType cellSet = nullptr;
  materialMapType materialMap = nullptr;
  bool useCellSet = false;
  NumericType cellSetDepth = 0.;

public:
  psDomain(bool passedUseCellSet = false) : useCellSet(passedUseCellSet) {
    levelSets = lsDomainsType::New();
    if (useCellSet) {
      cellSet = csDomainType::New();
    }
  }

  psDomain(lsDomainType passedLevelSet, bool passedUseCellSet = false,
           const NumericType passedDepth = 0.,
           const bool passedCellSetPosition = false)
      : useCellSet(passedUseCellSet), cellSetDepth(passedDepth) {
    levelSets = lsDomainsType::New();
    levelSets->push_back(passedLevelSet);
    // generate CellSet
    if (useCellSet) {
      cellSet =
          csDomainType::New(levelSets, cellSetDepth, passedCellSetPosition);
    }
  }

  psDomain(lsDomainsType passedLevelSets, bool passedUseCellSet = false,
           const NumericType passedDepth = 0.,
           const bool passedCellSetPosition = false)
      : useCellSet(passedUseCellSet), cellSetDepth(passedDepth) {
    levelSets = passedLevelSets;
    // generate CellSet
    if (useCellSet) {
      cellSet =
          csDomainType::New(levelSets, cellSetDepth, passedCellSetPosition);
    }
  }

  void deepCopy(psSmartPointer<psDomain> passedDomain) {
    levelSets->resize(passedDomain->levelSets->size());
    for (unsigned i = 0; i < levelSets->size(); ++i) {
      levelSets->at(i)->deepCopy(passedDomain->levelSets->at(i));
    }
    useCellSet = passedDomain->useCellSet;
    if (useCellSet) {
      cellSetDepth = passedDomain->cellSetDepth;
      cellSet->fromLevelSets(passedDomain->levelSets, passedDomain->materialMap,
                             cellSetDepth);
    }
    if (passedDomain->materialMap) {
      materialMap = materialMapType::New();
      for (std::size_t i = 0; i < passedDomain->materialMap->size(); i++) {
        materialMap->insertNextMaterial(
            passedDomain->materialMap->getMaterialAtIdx(i));
      }
    }
  }

  void insertNextLevelSet(lsDomainType passedLevelSet,
                          bool wrapLowerLevelSet = true) {
    if (!levelSets->empty() && wrapLowerLevelSet) {
      lsBooleanOperation<NumericType, D>(passedLevelSet, levelSets->back(),
                                         lsBooleanOperationEnum::UNION)
          .apply();
    }
    levelSets->push_back(passedLevelSet);
  }

  void insertNextLevelSetAsMaterial(lsDomainType passedLevelSet,
                                    const psMaterial material,
                                    bool wrapLowerLevelSet = true) {
    if (!levelSets->empty() && wrapLowerLevelSet) {
      lsBooleanOperation<NumericType, D>(passedLevelSet, levelSets->back(),
                                         lsBooleanOperationEnum::UNION)
          .apply();
    }
    if (!materialMap) {
      materialMap = materialMapType::New();
    }
    materialMap->insertNextMaterial(material);
    levelSets->push_back(passedLevelSet);
  }

  // copy the top LS and insert it in the domain (used to capture depositing
  // material)
  void duplicateTopLevelSet(const psMaterial material = psMaterial::Undefined) {
    if (levelSets->empty()) {
      return;
    }

    auto copy = lsDomainType::New(levelSets->back());
    if (material == psMaterial::Undefined) {
      insertNextLevelSet(copy, false);
    } else {
      insertNextLevelSetAsMaterial(copy, material, false);
    }
  }

  void setMaterialMap(materialMapType passedMaterialMap) {
    materialMap = passedMaterialMap;
  }

  // remove the top LS
  void removeTopLevelSet() {
    if (levelSets->empty()) {
      return;
    }

    levelSets->pop_back();
    if (materialMap) {
      auto newMatMap = materialMapType::New();
      for (std::size_t i = 0; i < levelSets->size(); i++) {
        newMatMap->insertNextMaterial(materialMap->getMaterialAtIdx(i));
      }
      materialMap = newMatMap;
    }
  }

  materialMapType getMaterialMap() const { return materialMap; }

  void generateCellSet(const NumericType depth = 0.,
                       const bool passedCellSetPosition = false) {
    useCellSet = true;
    cellSetDepth = depth;
    if (cellSet == nullptr) {
      cellSet = csDomainType::New();
    }
    cellSet->setCellSetPosition(passedCellSetPosition);
    cellSet->fromLevelSets(levelSets, materialMap, cellSetDepth);
  }

  auto &getLevelSets() { return levelSets; }

  auto &getCellSet() { return cellSet; }

  auto &getGrid() { return levelSets->back()->getGrid(); }

  void setUseCellSet(bool useCS) { useCellSet = useCS; }

  bool getUseCellSet() { return useCellSet; }

  void print() {
    std::cout << "Process Simulation Domain:" << std::endl;
    std::cout << "**************************" << std::endl;
    for (auto &ls : *levelSets) {
      ls->print();
    }
    std::cout << "**************************" << std::endl;
  }

  void printSurface(std::string name, bool addMaterialIds = true) {

    auto mesh = psSmartPointer<lsMesh<NumericType>>::New();

    if (addMaterialIds) {
      lsToDiskMesh<NumericType, D> meshConverter;
      meshConverter.setMesh(mesh);
      if (materialMap)
        meshConverter.setMaterialMap(materialMap->getMaterialMap());
      for (const auto ls : *levelSets) {
        meshConverter.insertNextLevelSet(ls);
      }
      meshConverter.apply();

      psSurfacePointValuesToLevelSet<NumericType, D>(levelSets->back(), mesh,
                                                     {"MaterialIds"})
          .apply();
    }

    lsToSurfaceMesh<NumericType, D>(levelSets->back(), mesh).apply();
    psVTKWriter<NumericType>(mesh, name).apply();
  }

  void writeLevelSets(std::string fileName) {
    for (int i = 0; i < levelSets->size(); i++) {
      lsWriter<NumericType, D>(
          levelSets->at(i), fileName + "_layer" + std::to_string(i) + ".lvst")
          .apply();
    }
  }

  void clear() {
    levelSets = lsDomainsType::New();
    if (useCellSet) {
      cellSet = csDomainType::New();
    }
  }
};

#endif // PS_DOMAIN_HPP
