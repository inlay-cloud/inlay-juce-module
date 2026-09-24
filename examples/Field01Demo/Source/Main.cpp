#include <JuceHeader.h>
#include "Field01Processor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new Field01Processor();
}
