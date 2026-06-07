// CLOUDS.h - Arduino wrapper for Mutable Instruments CLOUDS
// Note all the originals sources, .cc files, are retained and a copy with .inc extension has been made
//
#ifndef CLOUDS_ARDUINO_H_
#define CLOUDS_ARDUINO_H_

#include "src/clouds/dsp/audio_buffer.h"
#include "src/clouds/dsp/correlator.h"
#include "src/clouds/dsp/frame.h"

#include "src/clouds/dsp/fx/diffuser.h"
#include "src/clouds/dsp/fx/fx_engine.h"
#include "src/clouds/dsp/fx/pitch_shifter.h"
#include "src/clouds/dsp/fx/reverb.h"

#include "src/clouds/dsp/grain.h"
#include "src/clouds/dsp/granular_processor.h"
#include "src/clouds/dsp/granular_sample_player.h"
#include "src/clouds/dsp/looping_sample_player.h"
#include "src/clouds/dsp/mu_law.h"
#include "src/clouds/dsp/parameters.h"

#include "src/clouds/dsp/pvoc/frame_transformation.h"
#include "src/clouds/dsp/pvoc/phase_vocoder.h"
#include "src/clouds/dsp/pvoc/stft.h"

#include "src/clouds/dsp/sample_rate_converter.h"
#include "src/clouds/dsp/window.h"
#include "src/clouds/dsp/wsola_sample_player.h"
#include "src/clouds/resources.h"

#endif // CLOUDS_ARDUINO_H_
