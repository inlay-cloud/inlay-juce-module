#include <iostream>

#include <JuceHeader.h>

class ConsoleLogger final : public juce::Logger
{
public:
    void logMessage (const juce::String& message) override
    {
        std::cout << message << std::endl;
    }
};

class ConsoleUnitTestRunner final : public juce::UnitTestRunner
{
public:
    void logMessage (const juce::String& message) override
    {
        juce::Logger::writeToLog (message);
    }
};

int main()
{
    ConsoleLogger logger;
    juce::Logger::setCurrentLogger (&logger);

    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    ConsoleUnitTestRunner runner;
    runner.runTestsInCategory ("inlay_product_unlocking");

    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        if (auto* result = runner.getResult (i); result->failures > 0)
        {
            juce::Logger::setCurrentLogger (nullptr);
            return 1;
        }
    }

    juce::Logger::setCurrentLogger (nullptr);
    return 0;
}
