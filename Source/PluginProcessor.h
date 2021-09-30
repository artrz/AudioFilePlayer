/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

using namespace juce;
//==============================================================================
namespace Params
{
enum class Names
{
};

inline const std::map<Names, juce::String>& GetParamNames()
{
    static std::map<Names, juce::String> names =
    {
    };
    
    return names;
}
}
//==============================================================================
template<typename T, size_t Size = 30>
struct Fifo
{
    size_t getSize() const noexcept { return Size; }
    
    bool push(const T& t)
    {
        auto write = fifo.write(1);
        if( write.blockSize1 > 0 )
        {
            size_t index = static_cast<size_t>(write.startIndex1);
            buffer[index] = t;
            return true;
        }
        
        return false;
    }
    
    bool pull(T& t)
    {
        auto read = fifo.read(1);
        if( read.blockSize1 > 0 )
        {
            t = buffer[static_cast<size_t>(read.startIndex1)];
            return true;
        }
        
        return false;
    }
    
    int getNumAvailableForReading() const
    {
        return fifo.getNumReady();
    }
    
    int getAvailableSpace() const
    {
        return fifo.getFreeSpace();
    }
private:
    juce::AbstractFifo fifo { Size };
    std::array<T, Size> buffer;
};
//==============================================================================
template<typename ReferenceCountedType>
struct ReleasePool : juce::Timer
{
    ReleasePool()
    {
        deletionPool.reserve(5000);
        
        startTimer(1 * 1000);
    }
    
    ~ReleasePool() override
    {
        stopTimer();
    }
    
    using Ptr = typename ReferenceCountedType::Ptr;
    
    void add(Ptr ptr)
    {
        if( ptr == nullptr )
            return;
        
        if( juce::MessageManager::getInstance()->isThisTheMessageThread() )
        {
            addIfNotAlreadyThere(ptr);
        }
        else
        {
            if( fifo.push(ptr) )
            {
                successfullyAdded.set(true);
            }
            else
            {
                jassertfalse;
            }
        }
    }
    
    void timerCallback() override
    {
        if( successfullyAdded.compareAndSetBool(false, true))
        {
            Ptr ptr;
            while( fifo.pull(ptr) )
            {
                addIfNotAlreadyThere(ptr);
                ptr = nullptr;
            }
        }
        
        deletionPool.erase(std::remove_if(deletionPool.begin(),
                                          deletionPool.end(),
                                          [](const auto& ptr)
                                          {
                                              return ptr->getReferenceCount() <= 1;
                                          }),
                           deletionPool.end());
    }
private:
    Fifo<Ptr, 512> fifo;
    std::vector<Ptr> deletionPool;
    juce::Atomic<bool> successfullyAdded { false };
    
    void addIfNotAlreadyThere(Ptr ptr)
    {
        auto found = std::find_if(deletionPool.begin(),
                                  deletionPool.end(),
                                  [ptr](const auto& elem)
                                  {
                                      return elem.get() == ptr.get();
                                  });
        
        if( found == deletionPool.end() )
            deletionPool.push_back(ptr);
    }
};
//==============================================================================
struct ReferencedTransportSource : juce::ReferenceCountedObject
{
    using Ptr = juce::ReferenceCountedObjectPtr<ReferencedTransportSource>;
    
    std::unique_ptr<AudioFormatReaderSource> currentAudioFileSource;
    juce::AudioTransportSource transportSource;
    juce::URL currentAudioFile;
};

struct TransportSourceCreator : juce::Thread
{
    TransportSourceCreator(Fifo<ReferencedTransportSource::Ptr>& fifo,
                           ReleasePool<ReferencedTransportSource>& pool,
                           TimeSliceThread& tst,
                           AudioFormatManager& afm) :
    juce::Thread("TransportSourceCreator"),
    transportSourceFifo(fifo),
    releasePool(pool),
    directoryScannerBackgroundThread(tst),
    formatManager(afm)
    {
        startThread();
    }
    
    ~TransportSourceCreator()
    {
        stopThread(500);
    }
    
    void run() override
    {
        //create a referenced transport source if there is a new URL to load
        while( !threadShouldExit() )
        {
            if( urlNeedsProcessingFlag.compareAndSetBool(false, true) )
            {
                juce::URL audioURL;
                while( urlFifo.getNumAvailableForReading() > 0 )
                {
                    if( urlFifo.pull(audioURL) )
                    {
                        //create a new referenced transport source for this
                        std::unique_ptr<AudioFormatReader> reader;
                        
                        if (audioURL.isLocalFile())
                        {
                            reader.reset(formatManager.createReaderFor (audioURL.getLocalFile()));
                        }
                        else
                        {
                            reader.reset(formatManager.createReaderFor (audioURL.createInputStream (false)));
                        }
                        
                        if (reader != nullptr)
                        {
                            using RTS = ReferencedTransportSource;
                            RTS::Ptr rts = new ReferencedTransportSource();
                            rts->transportSource.prepareToPlay(blockSize.get(), sampleRate.get());
                            
                            auto sampleRate = reader->sampleRate;
                            
                            rts->currentAudioFileSource.reset (new AudioFormatReaderSource (reader.release(), true));
                            rts->currentAudioFile = audioURL;
                            
                            // ..and plug it into our transport source
                            rts->transportSource.setSource (rts->currentAudioFileSource.get(),
                                                       32768,                   // tells it to buffer this many samples ahead
                                                       &directoryScannerBackgroundThread,                 // this is the background thread to use for reading-ahead
                                                       sampleRate);     // allows for sample rate correction
                            
                            //add it to the release pool
                            releasePool.add(rts);
                            //add it to the transportSourceFifo
                            transportSourceFifo.push(rts);
                        }
                        
                    }
                }
            }
            
            wait( 5 );
        }
    }
    
    bool requestTransportForURL(juce::URL url)
    {
        if( urlFifo.push(url) )
        {
            urlNeedsProcessingFlag.set(true);
            return true;
        }
        
        return false;
    }
    
    void setSampleRate(double rate) { sampleRate.set( rate ); }
    void setBlockSize(int size) { blockSize.set( size ); }
private:
    Fifo<juce::URL> urlFifo;
    Fifo<ReferencedTransportSource::Ptr>& transportSourceFifo;
    ReleasePool<ReferencedTransportSource>& releasePool;
    
    TimeSliceThread& directoryScannerBackgroundThread;
    
    juce::Atomic<bool> urlNeedsProcessingFlag { false };
    
    AudioFormatManager& formatManager;
    
    juce::Atomic<double> sampleRate { 44100.0 };
    juce::Atomic<int> blockSize { 512 };
};
/**
*/
class AudioFilePlayerAudioProcessor  : public juce::AudioProcessor
{
public:
    //==============================================================================
    AudioFilePlayerAudioProcessor();
    ~AudioFilePlayerAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    using APVTS = juce::AudioProcessorValueTreeState;
    static APVTS::ParameterLayout createParameterLayout();
    APVTS apvts { *this, nullptr, "Properties", createParameterLayout() };
    juce::Atomic<bool> transportIsPlaying { false };
    
    TimeSliceThread directoryScannerBackgroundThread  { "audio file preview" };
    
    Fifo<ReferencedTransportSource::Ptr> fifo;
    ReleasePool<ReferencedTransportSource> pool;
    
    AudioFormatManager formatManager;
    TransportSourceCreator transportSourceCreator {fifo, pool, directoryScannerBackgroundThread, formatManager};
    
    ReferencedTransportSource::Ptr activeSource;
    
    template<typename SourceType>
    static void refreshCurrentFileInAPVTS(APVTS& apvts, SourceType& currentAudioFile)
    {
        auto file = currentAudioFile.getLocalFile();
        if( file.existsAsFile() )
        {
            apvts.state.setProperty("CurrentFile", file.getFullPathName(), nullptr);
        }
    }
private:
    void refreshTransportState();
    int maxBlockSize { 512 };
    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioFilePlayerAudioProcessor)
};
