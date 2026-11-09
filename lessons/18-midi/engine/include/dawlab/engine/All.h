#pragma once
// One include for the whole DawLab engine (chapter 15): Engine (transport, mixer with its track
// pool, queues, CPU meter), the effect adapters and factory, and the offline renderer.
//
//   #include <dawlab/engine/All.h>
//   using namespace dawlab::engine;

#include "Automation.h"
#include "ChannelStrip.h"
#include "Clip.h"
#include "Commands.h"
#include "Constants.h"
#include "CpuMeter.h"
#include "EffectBase.h"
#include "Effects.h"
#include "Engine.h"
#include "InsertEffect.h"
#include "LatencyCompensator.h"
#include "Mixer.h"
#include "Recording.h"
#include "RecordingSession.h"
#include "Renderer.h"
#include "SpscQueue.h"
#include "Track.h"
#include "Transport.h"
