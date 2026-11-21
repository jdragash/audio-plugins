# One JUCE for every plugin in the repo. Bumping the version here moves all of
# them, so build and test each plugin after a bump.
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.9
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)
