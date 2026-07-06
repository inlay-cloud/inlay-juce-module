/*
  ==============================================================================

   BEGIN_JUCE_MODULE_DECLARATION

    ID:                 inlay_product_unlocking
    vendor:             Inlay
    version:            0.1.0
    name:               Inlay Product Unlocking
    description:        Licensing, activation UI, and browser-based unlock flow for JUCE audio plugins.
    website:            https://inlay.cloud
    license:            Proprietary
    minimumCppStandard: 17

    dependencies:       juce_core, juce_cryptography, juce_events, juce_gui_basics, juce_product_unlocking

   END_JUCE_MODULE_DECLARATION

  ==============================================================================
*/

#pragma once

#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_product_unlocking/juce_product_unlocking.h>

#include "Unlocker.h"
#include "DefaultUI.h"
