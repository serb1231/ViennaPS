#ifndef DENSE_CELL_SET
#define DENSE_CELL_SET

#include <csBVH.hpp>
#include <csTracePath.hpp>
#include <csUtil.hpp>

#include <lsDomain.hpp>
#include <lsMakeGeometry.hpp>
#include <lsMesh.hpp>
#include <lsMessage.hpp>
#include <lsToVoxelMesh.hpp>
#include <lsVTKWriter.hpp>

#include <rayUtil.hpp>

#include <psSmartPointer.hpp>

/**
  This class represents a cell-based voxel implementation of a volume. The
  depth of the cell set in z-direction can be specified.
*/
template <class T, int D> class csDenseCellSet {
private:
  using gridType = psSmartPointer<lsMesh<T>>;
  using levelSetsType =
      psSmartPointer<std::vector<psSmartPointer<lsDomain<T, D>>>>;

  levelSetsType levelSets = nullptr;
  gridType cellGrid = nullptr;
  psSmartPointer<lsDomain<T, D>> surface = nullptr;
  psSmartPointer<csBVH<T, D>> BVH = nullptr;
  std::vector<std::set<unsigned long>> neighborhood;
  T gridDelta;
  size_t numberOfCells;
  T depth = 0.;
  T depthPlanePos = 0.;
  int BVHlayers = 0;
  bool cellSetAboveSurface = false;
  std::vector<T> *fillingFractions;

public:
  csDenseCellSet() {}

  csDenseCellSet(levelSetsType passedLevelSets, T passedDepth = 0.,
                 bool passedCellSetPosition = false)
      : levelSets(passedLevelSets), cellSetAboveSurface(passedCellSetPosition) {
    fromLevelSets(passedLevelSets, passedDepth);
  }

  void fromLevelSets(levelSetsType passedLevelSets, T passedDepth = 0.) {
    levelSets = passedLevelSets;

    if (cellGrid == nullptr)
      cellGrid = psSmartPointer<lsMesh<T>>::New();

    if (surface == nullptr)
      surface = psSmartPointer<lsDomain<T, D>>::New(levelSets->back());
    else
      surface->deepCopy(levelSets->back());

    gridDelta = surface->getGrid().getGridDelta();
    hrleVectorType<hrleIndexType, D> minBounds;
    hrleVectorType<hrleIndexType, D> maxBounds;
    for (unsigned i = 0; i < D; ++i) {
      minBounds[i] = std::numeric_limits<hrleIndexType>::max();
      maxBounds[i] = std::numeric_limits<hrleIndexType>::lowest();
    }
    for (unsigned i = 0; i < D; ++i) {
      minBounds[i] =
          std::min(minBounds[i], (surface->getGrid().isNegBoundaryInfinite(i))
                                     ? surface->getDomain().getMinRunBreak(i)
                                     : surface->getGrid().getMinBounds(i));

      maxBounds[i] =
          std::max(maxBounds[i], (surface->getGrid().isPosBoundaryInfinite(i))
                                     ? surface->getDomain().getMaxRunBreak(i)
                                     : surface->getGrid().getMaxBounds(i));
    }

    depth = passedDepth;
    if (cellSetAboveSurface) {
      depthPlanePos = maxBounds[D - 1] * gridDelta + depth - gridDelta;
    } else {
      depthPlanePos = minBounds[D - 1] * gridDelta - depth + gridDelta;
    }

    lsToVoxelMesh<T, D> voxelConverter(cellGrid);
    auto plane = psSmartPointer<lsDomain<T, D>>::New(surface->getGrid());
    if (depth > 0.) {
      T origin[D] = {0.};
      T normal[D] = {0.};
      origin[D - 1] = depthPlanePos;
      normal[D - 1] = 1.;
      lsMakeGeometry<T, D>(plane,
                           psSmartPointer<lsPlane<T, D>>::New(origin, normal))
          .apply();
    }
    if (!cellSetAboveSurface && depth > 0.)
      voxelConverter.insertNextLevelSet(plane);
    for (auto ls : *levelSets)
      voxelConverter.insertNextLevelSet(ls);
    if (cellSetAboveSurface && depth > 0.) {
      voxelConverter.insertNextLevelSet(plane);
    }
    voxelConverter.apply();

    if (!cellSetAboveSurface)
      adjustMaterialIds();

    // create filling fractions as default scalar cell data
    numberOfCells = cellGrid->template getElements<(1 << D)>().size();
    std::vector<T> fillingFractionsTemp(numberOfCells, 0.);
    cellGrid->getCellData().insertNextScalarData(
        std::move(fillingFractionsTemp), "fillingFraction");
    fillingFractions = cellGrid->getCellData().getScalarData("fillingFraction");

    calculateBounds(minBounds, maxBounds);
    BVH = psSmartPointer<csBVH<T, D>>::New(getBoundingBox(), BVHlayers);
    buildBVH();
  }

  csPair<std::array<T, D>> getBoundingBox() const {
    if constexpr (D == 3)
      return csPair<csTriple<T>>{cellGrid->minimumExtent,
                                 cellGrid->maximumExtent};
    else
      return csPair<csPair<T>>{
          cellGrid->minimumExtent[0], cellGrid->minimumExtent[1],
          cellGrid->maximumExtent[0], cellGrid->maximumExtent[1]};
  }

  void addScalarData(std::string name, T initValue) {
    std::vector<T> newData(numberOfCells, initValue);
    cellGrid->getCellData().insertNextScalarData(std::move(newData), name);
  }

  gridType getCellGrid() { return cellGrid; }

  psSmartPointer<csBVH<T, D>> getBVH() const { return BVH; }

  T getDepth() const { return depth; }

  T getGridDelta() const { return gridDelta; }

  std::vector<std::array<T, 3>> &getNodes() const {
    return cellGrid->getNodes();
  }

  std::vector<std::array<unsigned, (1 << D)>> getElements() {
    return cellGrid->template getElements<(1 << D)>();
  }

  psSmartPointer<lsDomain<T, D>> getSurface() { return surface; }

  levelSetsType getLevelSets() const { return levelSets; }

  size_t getNumberOfCells() const { return numberOfCells; }

  std::vector<T> *getFillingFractions() const { return fillingFractions; }

  T getFillingFraction(const std::array<T, D> &point) {
    auto idx = findIndex(point);
    if (idx < 0)
      return -1.;

    return getFillingFractions()->at(idx);
  }

  int getIndex(std::array<T, 3> &point) { return findIndex(point); }

  std::vector<T> *getScalarData(std::string name) {
    return cellGrid->getCellData().getScalarData(name);
  }

  // Set whether the cell set should be created below (false) or above (true)
  // the surface.
  void setCellSetPosition(const bool passedCellSetPosition) {
    cellSetAboveSurface = passedCellSetPosition;
  }

  // Sets the filling fraction at given cell index.
  bool setFillingFraction(const int idx, const T fill) {
    if (idx < 0)
      return false;

    getFillingFractions()->at(idx) = fill;
    return true;
  }

  // Sets the filling fraction for cell which contains given point.
  bool setFillingFraction(const std::array<T, 3> &point, const T fill) {
    auto idx = findIndex(point);
    return setFillingFraction(idx, fill);
  }

  // Add to the filling fraction at given cell index.
  bool addFillingFraction(int idx, T fill) {
    if (idx < 0)
      return false;

    getFillingFractions()->at(idx) += fill;
    return true;
  }

  // Add to the filling fraction for cell which contains given point.
  bool addFillingFraction(const std::array<T, 3> &point, T fill) {
    auto idx = findIndex(point);
    return addFillingFraction(idx, fill);
  }

  // Add to the filling fraction for cell which contains given point only if the
  // cell has the specified material ID.
  bool addFillingFractioninMaterial(const std::array<T, 3> &point, T fill,
                                    int materialId) {
    auto idx = findIndex(point);
    if (getScalarData("Material")->at(idx) == materialId)
      return addFillingFraction(idx, fill);
    else
      return false;
  }

  // Write the cell set as .vtu file
  void writeVTU(std::string fileName) {
    lsVTKWriter<T>(cellGrid, fileName).apply();
  }

  // Clear the filling fractions
  void clear() {
    auto ff = getFillingFractions();
    std::fill(ff->begin(), ff->end(), 0.);
  }

  // Update the material IDs of the cell set. This function should be called if
  // the level sets, the cell set is made out of, have changed. This does not
  // work if the surface of the volume has changed. In this case, call the
  // funciton update surface first.
  void updateMaterials() {
    auto numScalarData = cellGrid->getCellData().getScalarDataSize();
    std::vector<std::vector<T>> scalarData(numScalarData - 1);
    std::vector<std::string> scalarDataLabels(numScalarData - 1);

    // carry over all scalar data (except the material IDs)
    int n = 0;
    for (int i = 0; i < numScalarData; i++) {
      auto label = cellGrid->getCellData().getScalarDataLabel(i);
      if (label == "Material")
        continue;
      auto data = cellGrid->getCellData().getScalarData(i);
      scalarData[n] = std::move(*data);
      scalarDataLabels[n++] = label;
    }

    lsToVoxelMesh<T, D> voxelConverter(cellGrid);
    auto plane =
        psSmartPointer<lsDomain<T, D>>::New(levelSets->back()->getGrid());
    if (depth > 0.) {
      T origin[D] = {0.};
      T normal[D] = {0.};
      origin[D - 1] = depthPlanePos;
      normal[D - 1] = 1.;
      lsMakeGeometry<T, D>(plane,
                           psSmartPointer<lsPlane<T, D>>::New(origin, normal))
          .apply();
    }
    if (!cellSetAboveSurface && depth > 0.)
      voxelConverter.insertNextLevelSet(plane);
    for (auto ls : *levelSets)
      voxelConverter.insertNextLevelSet(ls);
    if (cellSetAboveSurface && depth > 0.) {
      voxelConverter.insertNextLevelSet(plane);
    }
    voxelConverter.apply();

    if (numberOfCells != cellGrid->template getElements<(1 << D)>().size()) {
      lsMessage::getInstance()
          .addWarning("Number of cells not equal in cell set material update. "
                      "The surface top might has changed.")
          .print();
      return;
    }

    for (int i = 0; i < numScalarData - 1; i++) {
      cellGrid->getCellData().insertNextScalarData(std::move(scalarData[i]),
                                                   scalarDataLabels[i]);
    }
    fillingFractions = cellGrid->getCellData().getScalarData("fillingFraction");
  }

  // Updates the surface of the cell set. The new surface should be below the
  // old surface as this function can only remove cells from the cell set.
  void updateSurface() {
    auto updateCellGrid = psSmartPointer<lsMesh<T>>::New();

    lsToVoxelMesh<T, D> voxelConverter(updateCellGrid);
    if (depth != 0.) {
      auto plane = psSmartPointer<lsDomain<T, D>>::New(surface->getGrid());
      T origin[D] = {0.};
      T normal[D] = {0.};
      origin[D - 1] = depthPlanePos;
      normal[D - 1] = 1.;

      lsMakeGeometry<T, D>(plane,
                           psSmartPointer<lsPlane<T, D>>::New(origin, normal))
          .apply();
      voxelConverter.insertNextLevelSet(plane);
    }
    voxelConverter.insertNextLevelSet(levelSets->back());
    voxelConverter.insertNextLevelSet(surface);
    voxelConverter.apply();

    auto cutMatIds = updateCellGrid->getCellData().getScalarData("Material");
    auto &hexas = cellGrid->template getElements<(1 << D)>();

    const auto nCutCells =
        updateCellGrid->template getElements<(1 << D)>().size();

    auto numScalarData = cellGrid->getCellData().getScalarDataSize();

    for (int elIdx = nCutCells - 1; elIdx >= 0; elIdx--) {
      if (cutMatIds->at(elIdx) == 2) {
        for (int i = 0; i < numScalarData; i++) {
          auto data = cellGrid->getCellData().getScalarData(i);
          data->erase(data->begin() + elIdx);
        }
        hexas.erase(hexas.begin() + elIdx);
      }
    }
    numberOfCells = hexas.size();
    surface->deepCopy(levelSets->back());

    buildBVH();
  }

  // Merge a trace path to the cell set.
  void mergePath(csTracePath<T> &path, T factor = 1.) {
    auto ff = getFillingFractions();
    if (!path.getData().empty()) {
      for (const auto it : path.getData()) {
        ff->at(it.first) += it.second / factor;
      }
    }

    if (!path.getGridData().empty()) {
      const auto &data = path.getGridData();
      for (size_t idx = 0; idx < numberOfCells; idx++) {
        ff->at(idx) += data[idx] / factor;
      }
    }
  }

  void buildNeighborhood() {
    const auto &cells = cellGrid->template getElements<(1 << D)>();
    unsigned const numNodes = cellGrid->getNodes().size();
    unsigned const numCells = cells.size();

    neighborhood.clear();
    neighborhood.resize(numCells);

    std::vector<std::vector<unsigned>> nodeCellConnections(numNodes);

    // for each node, store which cells are connected with the node
    for (unsigned cellIdx = 0; cellIdx < numCells; cellIdx++) {
      for (unsigned cellNodeIdx = 0; cellNodeIdx < (1 << D); cellNodeIdx++) {
        nodeCellConnections[cells[cellIdx][cellNodeIdx]].push_back(cellIdx);
      }
    }

    for (unsigned cellIdx = 0; cellIdx < numCells; cellIdx++) {
      for (unsigned cellNodeIdx = 0; cellNodeIdx < (1 << D); cellNodeIdx++) {
        auto &cellsAtNode = nodeCellConnections[cells[cellIdx][cellNodeIdx]];
        for (const auto &neighborCell : cellsAtNode) {
          if (neighborCell != cellIdx) {
            neighborhood[cellIdx].insert(neighborCell);
          }
        }
      }
    }
  }

  std::set<size_t> &getNeighbors(size_t cellIdx) {
    assert(!neighborhood.empty() &&
           "Querying neighbors without creating neighborhood structure");
    assert(cellIdx < numberOfCells && "Cell idx out of bounds");
    return neighborhood[cellIdx];
  }

private:
  int findIndex(const csTriple<T> &point) {
    const auto &elems = cellGrid->template getElements<(1 << D)>();
    const auto &nodes = cellGrid->getNodes();
    int idx = -1;

    auto cellIds = BVH->getCellIds(point);
    if (!cellIds)
      return idx;
    for (const auto cellId : *cellIds) {
      if (isInsideVoxel(point, nodes[elems[cellId][0]])) {
        idx = cellId;
        break;
      }
    }
    return idx;
  }

  void adjustMaterialIds() {
    auto matIds = getScalarData("Material");

#pragma omp parallel for
    for (size_t i = 0; i < matIds->size(); i++) {
      if (matIds->at(i) > 0) {
        matIds->at(i) -= 1;
      }
    }
  }

  int findSurfaceHitPoint(csTriple<T> &hitPoint, const csTriple<T> &direction) {
    // find surface hitpoint
    auto idx = findIndex(hitPoint);

    if (idx > 0)
      return idx;

    auto moveDirection = multNew(direction, gridDelta / 2.);
    size_t sanityCounter = 0;
    while (idx < 0) {
      add(hitPoint, moveDirection);
      if (++sanityCounter > 100 || !checkBoundsPeriodic(hitPoint)) {
        return -1;
      }
      idx = findIndex(hitPoint);
    }

    return idx;
  }

  bool isInsideVoxel(const csTriple<T> &point, const csTriple<T> &cellMin) {
    if constexpr (D == 3)
      return point[0] >= cellMin[0] && point[0] <= (cellMin[0] + gridDelta) &&
             point[1] >= cellMin[1] && point[1] <= (cellMin[1] + gridDelta) &&
             point[2] >= cellMin[2] && point[2] <= (cellMin[2] + gridDelta);
    else
      return point[0] >= cellMin[0] && point[0] <= (cellMin[0] + gridDelta) &&
             point[1] >= cellMin[1] && point[1] <= (cellMin[1] + gridDelta);
  }

  void buildBVH() {
    auto &elems = cellGrid->template getElements<(1 << D)>();
    auto &nodes = cellGrid->getNodes();
    BVH->clearCellIds();

    for (size_t elemIdx = 0; elemIdx < elems.size(); elemIdx++) {
      for (size_t n = 0; n < (1 << D); n++) {
        auto &node = nodes[elems[elemIdx][n]];
        BVH->getCellIds(node)->insert(elemIdx);
      }
    }
  }

  void calculateBounds(const hrleVectorType<hrleIndexType, D> &minBounds,
                       const hrleVectorType<hrleIndexType, D> &maxBounds) {
    constexpr T eps = 1e-4;
    cellGrid->minimumExtent[0] = minBounds[0] * gridDelta - eps;
    cellGrid->maximumExtent[0] = maxBounds[0] * gridDelta + eps;
    if constexpr (D == 3) {
      cellGrid->minimumExtent[1] = minBounds[1] * gridDelta - eps;
      cellGrid->maximumExtent[1] = maxBounds[1] * gridDelta + eps;
    }
    if (depth == 0.) {
      cellGrid->minimumExtent[D - 1] = minBounds[D - 1] * gridDelta - eps;
      cellGrid->maximumExtent[D - 1] = maxBounds[D - 1] * gridDelta + eps;
    } else if (!cellSetAboveSurface) {
      cellGrid->minimumExtent[D - 1] = depthPlanePos - gridDelta - eps;
      cellGrid->maximumExtent[D - 1] = maxBounds[D - 1] * gridDelta + eps;
    } else if (cellSetAboveSurface) {
      cellGrid->minimumExtent[D - 1] = minBounds[D - 1] * gridDelta - eps;
      cellGrid->maximumExtent[D - 1] = depthPlanePos + eps;
    }

    auto minExtent = cellGrid->maximumExtent[0] - cellGrid->minimumExtent[0];
    minExtent = std::min(minExtent, cellGrid->maximumExtent[1] -
                                        cellGrid->minimumExtent[1]);
    if constexpr (D == 3)
      minExtent = std::min(minExtent, cellGrid->maximumExtent[2] -
                                          cellGrid->minimumExtent[2]);

    BVHlayers = 0;
    while (minExtent / 2 > gridDelta) {
      BVHlayers++;
      minExtent /= 2;
    }
  }
};

#endif