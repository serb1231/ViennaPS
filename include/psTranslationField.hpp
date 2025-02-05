#ifndef PS_TRANSLATIONFIELD_HPP
#define PS_TRANSLATIONFIELD_HPP

#include <iostream>
#include <lsVelocityField.hpp>
#include <psKDTree.hpp>
#include <psVelocityField.hpp>

template <typename NumericType>
class psTranslationField : public lsVelocityField<NumericType> {
  using translatorType = std::unordered_map<unsigned long, unsigned long>;
  const int translationMethod = 1;

public:
  psTranslationField(
      psSmartPointer<psVelocityField<NumericType>> passedVeloField,
      psSmartPointer<psMaterialMap> passedMaterialMap)
      : translationMethod(passedVeloField->getTranslationFieldOptions()),
        modelVelocityField(passedVeloField), materialMap(passedMaterialMap) {}

  NumericType getScalarVelocity(const std::array<NumericType, 3> &coordinate,
                                int material,
                                const std::array<NumericType, 3> &normalVector,
                                unsigned long pointId) {
    if (translationMethod > 0)
      translateLsId(pointId, coordinate);
    if (materialMap)
      material = static_cast<int>(materialMap->getMaterialAtIdx(material));
    return modelVelocityField->getScalarVelocity(coordinate, material,
                                                 normalVector, pointId);
  }

  std::array<NumericType, 3>
  getVectorVelocity(const std::array<NumericType, 3> &coordinate, int material,
                    const std::array<NumericType, 3> &normalVector,
                    unsigned long pointId) {
    if (translationMethod > 0)
      translateLsId(pointId, coordinate);
    if (materialMap)
      material = static_cast<int>(materialMap->getMaterialAtIdx(material));
    return modelVelocityField->getVectorVelocity(coordinate, material,
                                                 normalVector, pointId);
  }

  NumericType
  getDissipationAlpha(int direction, int material,
                      const std::array<NumericType, 3> &centralDifferences) {
    if (materialMap)
      material = static_cast<int>(materialMap->getMaterialAtIdx(material));
    return modelVelocityField->getDissipationAlpha(direction, material,
                                                   centralDifferences);
  }

  void setTranslator(psSmartPointer<translatorType> passedTranslator) {
    translator = passedTranslator;
  }

  void buildKdTree(const std::vector<std::array<NumericType, 3>> &points) {
    kdTree.setPoints(points);
    kdTree.build();
  }

  void translateLsId(unsigned long &lsId,
                     const std::array<NumericType, 3> &coordinate) {
    if (translationMethod == 2) {
      auto nearest = kdTree.findNearest(coordinate);
      lsId = nearest->first;
    } else {
      if (auto it = translator->find(lsId); it != translator->end()) {
        lsId = it->second;
      } else {
        psLogger::getInstance()
            .addWarning("Could not extend velocity from surface to LS point")
            .print();
      }
    }
  }

private:
  psSmartPointer<translatorType> translator;
  psKDTree<NumericType, std::array<NumericType, 3>> kdTree;
  const psSmartPointer<psVelocityField<NumericType>> modelVelocityField;
  const psSmartPointer<psMaterialMap> materialMap;
};

#endif