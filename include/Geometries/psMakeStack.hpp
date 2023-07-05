#pragma once

#include <lsBooleanOperation.hpp>
#include <lsDomain.hpp>
#include <lsMakeGeometry.hpp>

#include <psDomain.hpp>

/**
 * Creates a stack of alternating SiO2/Si3N4 layers with an optional etched
 * hole(3D)/trench(2D) in the middle.
 */
template <class NumericType, int D> class psMakeStack {
  using PSPtrType = psSmartPointer<psDomain<NumericType, D>>;
  using LSPtrType = psSmartPointer<lsDomain<NumericType, D>>;

  PSPtrType domain = nullptr;

<<<<<<< HEAD
  const int numLayers = 11;
  const T layerHeight = 2;
  const T substrateHeight = 4;
  const T trenchWidth = 4;
=======
  const NumericType gridDelta;
  const NumericType xExtent;
  const NumericType yExtent;
  NumericType bounds[2 * D];
  NumericType normal[D];
  NumericType origin[D] = {0.};

  const int numLayers;
  const NumericType layerHeight;
  const NumericType substrateHeight;
  const NumericType holeRadius;
  const NumericType maskHeight;
>>>>>>> master
  const bool periodicBoundary = false;

  typename lsDomain<NumericType, D>::BoundaryType boundaryConds[D];

public:
<<<<<<< HEAD
  psMakeStack(PSPtrType passedDomain, const int passedNumLayers,
              const T passedGridDelta)
      : numLayers(passedNumLayers), gridDelta(passedGridDelta),
        geometry(passedDomain) {
    init();
  }

  psMakeStack(PSPtrType passedDomain, const T passedGridDelta,
              const T passedXExtent, const T passedYExtent,
              const int passedNumLayers, const T passedLayerHeight,
              const T passedSubstrateHeight, const T passedTrenchWidth,
              const bool periodic = false)
      : geometry(passedDomain), gridDelta(passedGridDelta),
        xExtent(passedXExtent), yExtent(passedYExtent),
        numLayers(passedNumLayers), layerHeight(passedLayerHeight),
        substrateHeight(passedSubstrateHeight), trenchWidth(passedTrenchWidth),
        periodicBoundary(periodic) {
=======
  psMakeStack(PSPtrType passedDomain, const NumericType passedGridDelta,
              const NumericType passedXExtent, const NumericType passedYExtent,
              const int passedNumLayers, const NumericType passedLayerHeight,
              const NumericType passedSubstrateHeight,
              const NumericType passedHoleRadius,
              const NumericType passedMaskHeight, const bool periodic = false)
      : domain(passedDomain), gridDelta(passedGridDelta),
        xExtent(passedXExtent), yExtent(passedYExtent),
        numLayers(passedNumLayers), layerHeight(passedLayerHeight),
        substrateHeight(passedSubstrateHeight), holeRadius(passedHoleRadius),
        maskHeight(passedMaskHeight), periodicBoundary(periodic) {
>>>>>>> master
    init();
  }

  void apply() {
    if constexpr (D == 2) {
      create2DGeometry();
    } else {
      create3DGeometry();
    }
  }

  int getTopLayer() const { return numLayers; }

<<<<<<< HEAD
  T getHeight() const { return substrateHeight + numLayers * layerHeight; }
=======
  NumericType getHeight() const {
    return substrateHeight + numLayers * layerHeight;
  }
>>>>>>> master

private:
  void create2DGeometry() {
    domain->clear();

    if (maskHeight > 0.) {
      // mask on top
      auto mask = LSPtrType::New(bounds, boundaryConds, gridDelta);
      origin[D - 1] = substrateHeight + layerHeight * numLayers + maskHeight;
      lsMakeGeometry<NumericType, D>(
          mask, lsSmartPointer<lsPlane<NumericType, D>>::New(origin, normal))
          .apply();

      auto maskAdd = LSPtrType::New(bounds, boundaryConds, gridDelta);
      origin[D - 1] = substrateHeight + layerHeight * numLayers;
      normal[D - 1] = -1;
      lsMakeGeometry<NumericType, D>(
          maskAdd, lsSmartPointer<lsPlane<NumericType, D>>::New(origin, normal))
          .apply();
      normal[D - 1] = 1.;

      lsBooleanOperation<NumericType, D>(mask, maskAdd,
                                         lsBooleanOperationEnum::INTERSECT)
          .apply();

      NumericType minPoint[D] = {
          -holeRadius, substrateHeight + layerHeight * numLayers - gridDelta};
      NumericType maxPoint[D] = {holeRadius, substrateHeight +
                                                 layerHeight * numLayers +
                                                 maskHeight + gridDelta};
      lsMakeGeometry<NumericType, D>(
          maskAdd,
          lsSmartPointer<lsBox<NumericType, D>>::New(minPoint, maxPoint))
          .apply();

      lsBooleanOperation<NumericType, D>(
          mask, maskAdd, lsBooleanOperationEnum::RELATIVE_COMPLEMENT)
          .apply();

      domain->insertNextLevelSetAsMaterial(mask, psMaterial::Mask);
    }

    // Silicon substrate
    auto substrate = LSPtrType::New(bounds, boundaryConds, gridDelta);
    origin[D - 1] = substrateHeight;
<<<<<<< HEAD
    auto plane = lsSmartPointer<lsPlane<T, D>>::New(origin, normal);
    lsMakeGeometry<T, D>(substrate, plane).apply();
    geometry->insertNextLevelSet(substrate);
=======
    lsMakeGeometry<NumericType, D>(
        substrate, lsSmartPointer<lsPlane<NumericType, D>>::New(origin, normal))
        .apply();
    domain->insertNextLevelSetAsMaterial(substrate, psMaterial::Si);
>>>>>>> master

    // Si3N4/SiO2 layers
    NumericType current = substrateHeight + layerHeight;
    for (int i = 0; i < numLayers; ++i) {
      auto ls = LSPtrType::New(bounds, boundaryConds, gridDelta);
      origin[D - 1] = substrateHeight + layerHeight * (i + 1);
      lsMakeGeometry<NumericType, D>(
          ls, lsSmartPointer<lsPlane<NumericType, D>>::New(origin, normal))
          .apply();
      if (i % 2 == 0) {
        domain->insertNextLevelSetAsMaterial(ls, psMaterial::SiO2);
      } else {
        domain->insertNextLevelSetAsMaterial(ls, psMaterial::Si3N4);
      }
    }

<<<<<<< HEAD
    // cut out middle
    auto cutOut = LSPtrType::New(bounds, boundaryConds, gridDelta);
    T minPoint[D] = {-trenchWidth / 2., 0.};
    T maxPoint[D] = {trenchWidth / 2.,
                     substrateHeight + layerHeight * numLayers + gridDelta};
    lsMakeGeometry<T, D>(cutOut,
                         lsSmartPointer<lsBox<T, D>>::New(minPoint, maxPoint))
        .apply();

    for (auto layer : *geometry->getLevelSets()) {
      lsBooleanOperation<T, D>(layer, cutOut,
                               lsBooleanOperationEnum::RELATIVE_COMPLEMENT)
=======
    if (holeRadius > 0. && maskHeight == 0.) {
      // cut out middle
      auto cutOut = LSPtrType::New(bounds, boundaryConds, gridDelta);
      NumericType minPoint[D] = {-holeRadius, 0.};
      NumericType maxPoint[D] = {holeRadius, substrateHeight +
                                                 layerHeight * numLayers +
                                                 maskHeight + gridDelta};
      lsMakeGeometry<NumericType, D>(
          cutOut,
          lsSmartPointer<lsBox<NumericType, D>>::New(minPoint, maxPoint))
>>>>>>> master
          .apply();

      for (auto layer : *domain->getLevelSets()) {
        lsBooleanOperation<NumericType, D>(
            layer, cutOut, lsBooleanOperationEnum::RELATIVE_COMPLEMENT)
            .apply();
      }
    }
  }

  void create3DGeometry() {
    domain->clear();

<<<<<<< HEAD
    // cut out middle
    auto cutOut = LSPtrType::New(bounds, boundaryConds, gridDelta);
    origin[D - 1] = 0.;
    {
      T minPoint[D] = {-trenchWidth / 2., -yExtent, 0.};
      T maxPoint[D] = {trenchWidth / 2., yExtent,
                       substrateHeight + layerHeight * numLayers + gridDelta};
      lsMakeGeometry<T, D>(cutOut,
                           lsSmartPointer<lsBox<T, D>>::New(minPoint, maxPoint))
          .apply();
=======
    if (maskHeight > 0.) {
      auto mask = LSPtrType::New(bounds, boundaryConds, gridDelta);
      origin[D - 1] = substrateHeight + layerHeight * numLayers + maskHeight;
      lsMakeGeometry<NumericType, D>(
          mask, lsSmartPointer<lsPlane<NumericType, D>>::New(origin, normal))
          .apply();

      auto maskAdd = LSPtrType::New(bounds, boundaryConds, gridDelta);
      origin[D - 1] = substrateHeight + layerHeight * numLayers;
      normal[D - 1] = -1;
      lsMakeGeometry<NumericType, D>(
          maskAdd, lsSmartPointer<lsPlane<NumericType, D>>::New(origin, normal))
          .apply();

      lsBooleanOperation<NumericType, D>(mask, maskAdd,
                                         lsBooleanOperationEnum::INTERSECT)
          .apply();

      normal[D - 1] = 1.;
      lsMakeGeometry<NumericType, D>(
          maskAdd, lsSmartPointer<lsCylinder<NumericType, D>>::New(
                       origin, normal, maskHeight + gridDelta, holeRadius))
          .apply();

      lsBooleanOperation<NumericType, D>(
          mask, maskAdd, lsBooleanOperationEnum::RELATIVE_COMPLEMENT)
          .apply();

      domain->insertNextLevelSetAsMaterial(mask, psMaterial::Mask);
>>>>>>> master
    }

    // Silicon substrate
    auto substrate = LSPtrType::New(bounds, boundaryConds, gridDelta);
    origin[D - 1] = substrateHeight;
    lsMakeGeometry<NumericType, D>(
        substrate, lsSmartPointer<lsPlane<NumericType, D>>::New(origin, normal))
        .apply();
<<<<<<< HEAD
    geometry->insertNextLevelSet(substrate);
=======
    domain->insertNextLevelSet(substrate, psMaterial::Si);
>>>>>>> master

    // Si3N4/SiO2 layers
    for (int i = 0; i < numLayers; ++i) {
      auto ls = LSPtrType::New(bounds, boundaryConds, gridDelta);
      origin[D - 1] = substrateHeight + layerHeight * (i + 1);
      lsMakeGeometry<NumericType, D>(
          ls, lsSmartPointer<lsPlane<NumericType, D>>::New(origin, normal))
          .apply();
      if (i % 2 == 0) {
        domain->insertNextLevelSetAsMaterial(ls, psMaterial::SiO2);
      } else {
        domain->insertNextLevelSetAsMaterial(ls, psMaterial::Si3N4);
      }
    }

    if (holeRadius > 0. && maskHeight == 0.) {
      // cut out middle
      auto cutOut = LSPtrType::New(bounds, boundaryConds, gridDelta);
      origin[D - 1] = 0.;
      lsMakeGeometry<NumericType, D>(
          cutOut,
          lsSmartPointer<lsCylinder<NumericType, D>>::New(
              origin, normal, (numLayers + 1) * layerHeight, holeRadius))
          .apply();

      for (auto layer : *domain->getLevelSets()) {
        lsBooleanOperation<NumericType, D>(
            layer, cutOut, lsBooleanOperationEnum::RELATIVE_COMPLEMENT)
            .apply();
      }
    }
  }

  void init() {
    bounds[0] = -xExtent;
    bounds[1] = xExtent;
    normal[0] = 0.;
    if (periodicBoundary)
      boundaryConds[0] =
          lsDomain<NumericType, D>::BoundaryType::PERIODIC_BOUNDARY;
    else
      boundaryConds[0] =
          lsDomain<NumericType, D>::BoundaryType::REFLECTIVE_BOUNDARY;

    if constexpr (D == 2) {
      normal[1] = 1.;
      bounds[2] = 0;
      bounds[3] = layerHeight * numLayers + gridDelta;
      boundaryConds[1] =
          lsDomain<NumericType, D>::BoundaryType::INFINITE_BOUNDARY;
    } else {
      normal[1] = 0.;
      normal[2] = 1.;
      bounds[2] = -yExtent;
      bounds[3] = yExtent;
      bounds[4] = 0;
      bounds[5] = layerHeight * numLayers + gridDelta;
      if (periodicBoundary)
        boundaryConds[0] =
            lsDomain<NumericType, D>::BoundaryType::PERIODIC_BOUNDARY;
      else
        boundaryConds[0] =
            lsDomain<NumericType, D>::BoundaryType::REFLECTIVE_BOUNDARY;
      boundaryConds[2] =
          lsDomain<NumericType, D>::BoundaryType::INFINITE_BOUNDARY;
    }
  }
};