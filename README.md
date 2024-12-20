# AudioFilePlayer
A port of the juce AudioFilePlayerDemo into Plugin format

When you clone this repository, be sure to enable "RECURSE SUBMODULES" in whatever tool you're using the clone, so that the correct version of juce that this plugin is built with is also cloned.

This project contains a submodule of the JUCE framework. This submodule set up to automatically check out the v8.0.4 version of JUCE.

If you're using same major version of JUCE you may want to try compiling the plugin with it first. Don't forget to point the module paths to the correct location.

When building the plugin with the version of Projucer that is within this submodule (JUCE/Extras/Projucer/Builds/), and using that build of projucer to open the AudioFilePlayer.jucer file everything should "just work" when you:
- open the AudioFilePlayer.jucer file,
- save/open in IDE, and then
- compile it for your operating system.
  - MacOS: xcodebuild -project Builds/MacOSX/AudioFilePlayer.xcodeproj -configuration Release

you should experience zero errors if you use the submodule's projucer build to generate the SLN/XCodeProj files.

If you experience errors when building the plugin with the submodule version after trying first with a more recent JUCE version, delete all autogenerated directories. If you're on MacOS, delete the subdirectory generated inside DerivedData/ too.
