//--------------------------------------------------------------------------------------------------
// DemoFiltering.cpp
//
// Logic for the demo of the same name
//
//--------------------------------------------------------------------------------------------------

#include "DemoMgr.h"
#include "AudioEffects.h"
#include <algorithm>
#include "Samples.h"

namespace DemoFiltering {

    enum EWaveForm {
        e_waveSine,
        e_waveSaw,
        e_waveSquare,
        e_waveTriangle,

        e_sampleCymbals,
        e_sampleVoice,
    };

    enum EEffect {
        e_none,
        e_small,
        e_medium,
        e_large,
        e_LFO,

        e_effectCount
    };

    const char* EffectToString (EEffect effect) {
        switch (effect) {
            case e_none: return "none";
            case e_small: return "small";
            case e_medium: return "medium";
            case e_large: return "large";
            case e_LFO: return "LFO";
        }
        return "??";
    }

    const char* WaveFormToString (EWaveForm waveForm) {
        switch (waveForm) {
            case e_waveSine: return "Sine";
            case e_waveSaw: return "Saw";
            case e_waveSquare: return "Square";
            case e_waveTriangle: return "Triangle";
        }
        return "???";
    }

    struct SNote {
        SNote(float frequency, EWaveForm waveForm)
            : m_frequency(frequency)
            , m_waveForm(waveForm)
            , m_age(0)
            , m_dead(false)
            , m_wantsKeyRelease(false)
            , m_releaseAge(0) {}

        float       m_frequency;
        EWaveForm   m_waveForm;
        size_t      m_age;
        bool        m_dead;
        bool        m_wantsKeyRelease;
        size_t      m_releaseAge;
    };

    std::vector<SNote>  g_notes;
    std::mutex          g_notesMutex;
    EWaveForm           g_currentWaveForm;

    EEffect             g_lpf;
    EEffect             g_hpf;

    bool                g_rhythmOn;
    bool                g_masterOutLPFOn;

    //--------------------------------------------------------------------------------------------------
    void OnInit() { }

    //--------------------------------------------------------------------------------------------------
    void OnExit() { }

    //--------------------------------------------------------------------------------------------------
    inline float GenerateEnvelope (SNote& note, float ageInSeconds, float sampleRate) {

        // this just puts a short envelope on the beginning and end of the note and kills the note
        // when the release envelope is done.

        float envelope = 0.0f;

        static const float c_envelopeTime = 0.1f;

        // if the key isn't yet released
        if (note.m_releaseAge == 0) {
            // release the key if it wants to be released and has done the intro envelope
            if (note.m_wantsKeyRelease && ageInSeconds > c_envelopeTime) {
                note.m_releaseAge = note.m_age;
            }
            // else do the intro envelope
            else {
                envelope = Envelope2Pt(
                    ageInSeconds,
                    0.0f, 0.0f,
                    c_envelopeTime, 1.0f
                );
            }
        }

        // if the key has been released, apply the outro envelope
        if (note.m_releaseAge != 0) {

            float releaseAgeInSeconds = float(note.m_releaseAge) / sampleRate;

            float secondsInRelease = ageInSeconds - releaseAgeInSeconds;

            envelope = Envelope2Pt(
                secondsInRelease,
                0.0f, 1.0f,
                c_envelopeTime, 0.0f
            );

            // kill the note when the release is done
            if (secondsInRelease > c_envelopeTime)
                note.m_dead = true;
        }

        return envelope;
    }

    //--------------------------------------------------------------------------------------------------
    inline float SampleAudioSample(SNote& note, SWavFile& sample, float ageInSeconds) {

        // handle the note dieing when it is done
        size_t sampleIndex = note.m_age*sample.m_numChannels;
        if (sampleIndex >= sample.m_numSamples) {
            note.m_dead = true;
            return 0.0f;
        }

        // calculate and apply an envelope to the sound samples
        float envelope = Envelope4Pt(
            ageInSeconds,
            0.0f, 0.0f,
            0.1f, 1.0f,
            sample.m_lengthSeconds - 0.1f, 1.0f,
            sample.m_lengthSeconds, 0.0f
        );

        // return the sample value multiplied by the envelope
        return sample.m_samples[sampleIndex] * envelope;
    }

    //--------------------------------------------------------------------------------------------------
    inline float GenerateNoteSample (SNote& note, float sampleRate) {

        // calculate our age in seconds and advance our age in samples, by 1 sample
        float ageInSeconds = float(note.m_age) / sampleRate;
        ++note.m_age;

        // generate the envelope value for our note
        // decrease note volume a bit, because the volume adjustments don't seem to be quite enough
        float envelope = GenerateEnvelope(note, ageInSeconds, sampleRate) * 0.8f;

        // generate the audio sample value for the current time.
        // Note that it is ok that we are basing audio samples on age instead of phase, because the
        // frequency never changes and we envelope the front and back to avoid popping.
        float phase = std::fmodf(ageInSeconds * note.m_frequency, 1.0f);
        switch (note.m_waveForm) {
            case e_waveSine:        return SineWave(phase) * envelope;
            case e_waveSaw:         return SawWave(phase) * envelope;
            case e_waveSquare:      return SquareWave(phase)  * envelope;
            case e_waveTriangle:    return TriangleWave(phase)  * envelope;
            case e_sampleCymbals:   return SampleAudioSample(note, g_sample_cymbal, ageInSeconds);
            case e_sampleVoice:     return SampleAudioSample(note, g_sample_legend1, ageInSeconds);
        }

        return 0.0f;
    }

    //--------------------------------------------------------------------------------------------------
    float GenerateRhythm (size_t sampleIndex, float sampleRate) {
        size_t beatSize = size_t(sampleRate) / 8;
        float beatTime = float(beatSize) / sampleRate;

        size_t beatIndex = sampleIndex / beatSize;
        size_t beatOffset = sampleIndex % beatSize;
        float timeInSeconds = float(beatOffset) / sampleRate;
        
        beatIndex = beatIndex % 32;

        float frequency = 0.0f;
        if (beatIndex < 16) {
            beatIndex = beatIndex % 4;
            switch (beatIndex) {
                case 0: frequency = NoteToFrequency(2, 0); break;
                case 1: frequency = NoteToFrequency(1, 0); break;
                case 2: frequency = NoteToFrequency(2, 3); break;
                case 3: frequency = NoteToFrequency(1, 3); break;
            }
        }
        else {
            beatIndex = beatIndex % 4;
            switch (beatIndex) {
                case 0: frequency = NoteToFrequency(2, 2); break;
                case 1: frequency = NoteToFrequency(1, 2); break;
                case 2: frequency = NoteToFrequency(2, 5); break;
                case 3: frequency = NoteToFrequency(1, 5); break;
            }
        }

        float phase = std::fmodf(timeInSeconds * frequency, 1.0f);

        float envelope = Envelope3Pt(
            timeInSeconds,
            0.0f, 0.0f,
            0.1f, 1.0f,
            beatTime, 0.0f
        );

        switch (g_currentWaveForm) {
            case e_waveSine:        return SineWave(phase) * envelope;
            case e_waveSaw:         return SawWave(phase) * envelope;
            case e_waveSquare:      return SquareWave(phase) * envelope;
            case e_waveTriangle:    return TriangleWave(phase) * envelope;
        }
        return 0.0f;
    }

    //--------------------------------------------------------------------------------------------------
    void GenerateAudioSamples (float *outputBuffer, size_t framesPerBuffer, size_t numChannels, float sampleRate) {

        // size of resonating peak
        const float Q = 2.0f;
        static const int c_numFilters = 4;

        // a LPF to apply at the end to keep things from getting too gnarly
        static SBiQuad masterOutLPF;
        static bool masterOutLPFInited = false;
        if (!masterOutLPFInited) {
            masterOutLPF.SetEffectParams(SBiQuad::EType::e_lowPass, 440.0f, sampleRate, 1.0f, 1.0f);
            masterOutLPFInited = true;
        }
        bool masterOutLPFOn = g_masterOutLPFOn;

        // update our low pass filter
        static SBiQuad lowPassFilter[c_numFilters];
        static EEffect lastLPF = e_none;
        EEffect currentLPF = g_lpf;
        if (currentLPF != lastLPF) {
            lastLPF = currentLPF;
            switch (currentLPF) {
                case e_none: break;
                case e_small: {
                    for (int i = 0; i < c_numFilters; ++i)
                        lowPassFilter[i].SetEffectParams(SBiQuad::EType::e_lowPass, 1760.0f, sampleRate, Q, 1.0f);
                    break;
                }
                case e_medium: {
                    for (int i = 0; i < c_numFilters; ++i)
                        lowPassFilter[i].SetEffectParams(SBiQuad::EType::e_lowPass, 880.0f, sampleRate, Q, 1.0f);
                    break;
                }
                case e_large: {
                    for (int i = 0; i < c_numFilters; ++i)
                        lowPassFilter[i].SetEffectParams(SBiQuad::EType::e_lowPass, 220.0f, sampleRate, Q, 1.0f);
                    break;
                }
            }
        }

        static SBiQuad highPassFilter[c_numFilters];
        static EEffect lastHPF = e_none;
        EEffect currentHPF = g_hpf;
        if (currentHPF != lastHPF) {
            lastHPF = currentHPF;
            switch (currentHPF) {
                case e_none: break;
                case e_small: 
                    for (int i = 0; i < c_numFilters; ++i)
                        highPassFilter[i].SetEffectParams(SBiQuad::EType::e_highPass, 220.0f, sampleRate, Q, 1.0f);
                    break;
                case e_medium:
                    for (int i = 0; i < c_numFilters; ++i)
                        highPassFilter[i].SetEffectParams(SBiQuad::EType::e_highPass, 880.0f, sampleRate, Q, 1.0f);
                    break;
                case e_large:
                    for (int i = 0; i < c_numFilters; ++i)
                        highPassFilter[i].SetEffectParams(SBiQuad::EType::e_highPass, 1760.0f, sampleRate, Q, 1.0f);
                    break;
            }
        }

        // handle auto generated rhythm starting and stopping
        static bool rhythmWasOn = false;
        bool rhythmIsOn = g_rhythmOn;
        static size_t rhythmStart = 0;
        if (rhythmWasOn != rhythmIsOn) {
            rhythmWasOn = rhythmIsOn;
            rhythmStart = CDemoMgr::GetSampleClock();
        }

        // get a lock on our notes vector
        std::lock_guard<std::mutex> guard(g_notesMutex);

        // for every sample in our output buffer
        for (size_t sample = 0; sample < framesPerBuffer; ++sample, outputBuffer += numChannels) {
            
            // handle LFO controlled LPF
            if (currentLPF == e_LFO) {
                float LFOValue = std::sinf(float(CDemoMgr::GetSampleClock()) * (1.0f / 7.0f) / sampleRate*2.0f*c_pi);
                float LFOfrequency = ScaleBiPolarValue(LFOValue, 250, 1500);
                for (int i = 0; i < c_numFilters; ++i)
                    lowPassFilter[i].SetEffectParams(SBiQuad::EType::e_lowPass, LFOfrequency, sampleRate, Q, 1.0f);
            }

            // handle LFO controlled HPF
            if (currentHPF == e_LFO) {
                float LFOfrequency = std::sinf(float(CDemoMgr::GetSampleClock()) * 0.125f / sampleRate*2.0f*c_pi) * 225.0f + 450.0f;
                for (int i = 0; i < c_numFilters; ++i)
                    highPassFilter[i].SetEffectParams(SBiQuad::EType::e_highPass, LFOfrequency, sampleRate, Q, 1.0f);
            }

            // add up all notes to get the final value.
            float value = 0.0f;
            std::for_each(
                g_notes.begin(),
                g_notes.end(),
                [&value, sampleRate](SNote& note) {
                    value += GenerateNoteSample(note, sampleRate);
                }
            );

            // generate rhythm notes if we should
            if (rhythmIsOn)
                value += GenerateRhythm(CDemoMgr::GetSampleClock() - rhythmStart + sample, sampleRate);

            // apply lpf
            if (currentLPF != e_none) {
                for (int i = 0; i < c_numFilters; ++i)
                    value = lowPassFilter[i].AddSample(value);
            }

            // apply hpf
            if (currentHPF != e_none) {
                for (int i = 0; i < c_numFilters; ++i)
                    value = highPassFilter[i].AddSample(value);
            }

            // apply the final LPF if we should
            if (masterOutLPFOn)
                value = masterOutLPF.AddSample(value);

            // copy the value to all audio channels
            for (size_t channel = 0; channel < numChannels; ++channel)
                outputBuffer[channel] = value;
        }

        // remove notes that have died
        auto iter = std::remove_if(
            g_notes.begin(),
            g_notes.end(),
            [] (const SNote& note) {
                return note.m_dead;
            }
        );

        if (iter != g_notes.end())
            g_notes.erase(iter);
    }

    //--------------------------------------------------------------------------------------------------
    void StopNote (float frequency) {

        // get a lock on our notes vector
        std::lock_guard<std::mutex> guard(g_notesMutex);

        // Any note that is this frequency should note that it wants to enter released state.
        std::for_each(
            g_notes.begin(),
            g_notes.end(),
            [frequency] (SNote& note) {
                if (note.m_frequency == frequency) {
                    note.m_wantsKeyRelease = true;
                }
            }
        );
    }

    //--------------------------------------------------------------------------------------------------
    void ReportParams () {
        printf("Instrument: %s  LPF: %s  HPF: %s  master out lpf = %s\r\n", WaveFormToString(g_currentWaveForm), EffectToString(g_lpf), EffectToString(g_hpf), g_masterOutLPFOn ? "On" : "Off");
    }

    //--------------------------------------------------------------------------------------------------
    void OnKey (char key, bool pressed) {

        // pressing numbers switches instruments
        if (pressed) {
            switch (key)
            {
                case '1': g_currentWaveForm = e_waveSine; ReportParams(); return;
                case '2': g_currentWaveForm = e_waveSaw; ReportParams(); return;
                case '3': g_currentWaveForm = e_waveSquare; ReportParams(); return;
                case '4': g_currentWaveForm = e_waveTriangle; ReportParams(); return;
                case '5': {
                    std::lock_guard<std::mutex> guard(g_notesMutex);
                    g_notes.push_back(SNote(0.0f, e_sampleCymbals));
                    return;
                }
                case '6': {
                    std::lock_guard<std::mutex> guard(g_notesMutex);
                    g_notes.push_back(SNote(0.0f, e_sampleVoice));
                    return;
                }
                case '7': {
                    g_lpf = EEffect((g_lpf + 1) % e_effectCount);
                    ReportParams();
                    return;
                }
                case '8': {
                    g_hpf = EEffect((g_hpf + 1) % e_effectCount);
                    ReportParams();
                    return;
                }
                case '9': {
                    g_rhythmOn = !g_rhythmOn;
                    return;
                }
                case '0': {
                    g_masterOutLPFOn = !g_masterOutLPFOn;
                    ReportParams();
                    return;
                }
            }
        }

        // figure out what frequency to play
        float frequency = 0.0f;
        switch (key) {

            // QWERTY row
            case 'Q': frequency = NoteToFrequency(3, 0); break;
            case 'W': frequency = NoteToFrequency(3, 1); break;
            case 'E': frequency = NoteToFrequency(3, 2); break;
            case 'R': frequency = NoteToFrequency(3, 3); break;
            case 'T': frequency = NoteToFrequency(3, 4); break;
            case 'Y': frequency = NoteToFrequency(3, 5); break;
            case 'U': frequency = NoteToFrequency(3, 6); break;
            case 'I': frequency = NoteToFrequency(3, 7); break;
            case 'O': frequency = NoteToFrequency(3, 8); break;
            case 'P': frequency = NoteToFrequency(3, 9); break;
            case -37: frequency = NoteToFrequency(3, 10); break;

            // ASDF row
            case 'A': frequency = NoteToFrequency(2, 0); break;
            case 'S': frequency = NoteToFrequency(2, 1); break;
            case 'D': frequency = NoteToFrequency(2, 2); break;
            case 'F': frequency = NoteToFrequency(2, 3); break;
            case 'G': frequency = NoteToFrequency(2, 4); break;
            case 'H': frequency = NoteToFrequency(2, 5); break;
            case 'J': frequency = NoteToFrequency(2, 6); break;
            case 'K': frequency = NoteToFrequency(2, 7); break;
            case 'L': frequency = NoteToFrequency(2, 8); break;
            case -70: frequency = NoteToFrequency(2, 9); break;
            case -34: frequency = NoteToFrequency(2, 10); break;

            // ZXCV row
            case 'Z': frequency = NoteToFrequency(1, 0); break;
            case 'X': frequency = NoteToFrequency(1, 1); break;
            case 'C': frequency = NoteToFrequency(1, 2); break;
            case 'V': frequency = NoteToFrequency(1, 3); break;
            case 'B': frequency = NoteToFrequency(1, 4); break;
            case 'N': frequency = NoteToFrequency(1, 5); break;
            case 'M': frequency = NoteToFrequency(1, 6); break;
            case -68: frequency = NoteToFrequency(1, 7); break;
            case -66: frequency = NoteToFrequency(1, 8); break;
            case -65: frequency = NoteToFrequency(1, 9); break;
            case -95: frequency = NoteToFrequency(1, 10); break;  // right shift

            // left shift = low freq
            case 16: frequency = NoteToFrequency(0, 5); break;

            // left shift = low freq
            case -94: frequency = NoteToFrequency(0, 0); break;

            default: {
                return;
            }
        }

        // if releasing a note, we need to find and kill the flute note of the same frequency
        if (!pressed) {
            StopNote(frequency);
            return;
        }

        float time = float(CDemoMgr::GetSampleClock()) / CDemoMgr::GetSampleRate();
        printf("%c : %0.2f\r\n", key, time);

        // get a lock on our notes vector and add the new note
        std::lock_guard<std::mutex> guard(g_notesMutex);
        g_notes.push_back(SNote(frequency, g_currentWaveForm));
    }

    //--------------------------------------------------------------------------------------------------
    void OnEnterDemo () {
        g_currentWaveForm = e_waveSine;
        g_lpf = e_none;
        g_hpf = e_none;
        g_rhythmOn = false;
        g_masterOutLPFOn = false;
        printf("Letter keys to play notes.\r\nleft shift / control is super low frequency.\r\n");
        printf("1 = Sine\r\n");
        printf("2 = Saw\r\n");
        printf("3 = Square\r\n");
        printf("4 = Triangle\r\n");
        printf("5 = cymbals sample\r\n");
        printf("6 = voice sample\r\n");
        printf("7 = cycle Low Pass Filter\r\n");
        printf("8 = cycle High Pass Filter\r\n");
        printf("9 = Toggle afzv / dhcn\r\n");
        printf("0 = toggle master out lpf\r\n");
        printf("\r\nInstructions:\r\n");
        printf("show how lpf / hpf work, then show some notes on LFO.\r\n");
        printf("sine not as interesting, not as much to cut away.\r\n");
        printf("LFO LPF = afzv time 4 then dhcn times 4\r\nAlso = afqt\r\n");
        printf("Toggle clipping for some awesome sounds!\r\n");

        // clear all the notes out
        std::lock_guard<std::mutex> guard(g_notesMutex);
        g_notes.clear();
    }
}
