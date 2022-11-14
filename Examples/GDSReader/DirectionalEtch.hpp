#pragma once

#include <iostream>
#include <vector>

#include <psProcessModel.hpp>
#include <psSmartPointer.hpp>
#include <psSurfaceModel.hpp>
#include <psVelocityField.hpp>

template <class T>
class DirectionalEtchVelocityField : public psVelocityField<T> {
public:
  DirectionalEtchVelocityField(const int matId) : etchMat(matId) {}

  T getScalarVelocity(const std::array<T, 3> & /*coordinate*/, int material,
                      const std::array<T, 3> &normalVector,
                      unsigned long pointID) override {
    // etch directionally
    if (material == etchMat) {
      return (normalVector[2] > 0.4) ? -normalVector[2] * rate : 0;
    } else {
      return 0;
    }
  }

  void setVelocities(psSmartPointer<std::vector<T>> passedVelocities) override {
    // additional alerations can be made to the velocities here
    velocities = passedVelocities;
  }

private:
  psSmartPointer<std::vector<T>> velocities = nullptr;
  const T rate = 0.1;
  const int etchMat = 0;
};

template <typename NumericType>
class DirectionalEtchSurfaceModel : public psSurfaceModel<NumericType> {

public:
  psSmartPointer<std::vector<NumericType>>
  calculateVelocities(psSmartPointer<psPointData<NumericType>> Rates,
                      const std::vector<NumericType> &materialIds) override {
    return nullptr;
  }
};