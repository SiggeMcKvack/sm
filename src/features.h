// This file declares extensions to the base game
#ifndef SM_FEATURES_H_
#define SM_FEATURES_H_

#include "types.h"

// Feature flags for enhanced features
enum EnhancedFeatures0 {
  kFeatures0_ExtendScreen64 = 1,        // Extend sprite boundaries by 64 pixels for widescreen
  kFeatures0_WidescreenVisualFixes = 2, // Enable widescreen-specific visual fixes
};

extern uint8 enhanced_features0;

#endif  // SM_FEATURES_H_
