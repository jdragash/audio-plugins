# Code shared between plugins.
#
# shared_dsp: plain C++ DSP building blocks, header only, no JUCE.
#   Link it and include as "shared/dsp/<Name>.h".
add_library(shared_dsp INTERFACE)
target_include_directories(shared_dsp INTERFACE ${PROJECT_SOURCE_DIR})
