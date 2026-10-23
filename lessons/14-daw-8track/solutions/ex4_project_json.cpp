// Exercise 4 (solution): save and load a project as JSON (pure C++, no library).
//
// What is in a project file
//   - transport: tempo, time signature, loop
//   - mixer: PDC on/off; for every strip (8 tracks, 2 buses, master):
//       name, fader, pan, pan law, pan mode, mute, solo, sends (level + pre/post),
//       inserts (effect type name, bypass, every parameter BY NAME), and for tracks the clips
//   - clips reference audio by an id in a "media pool" (here: a std::map of shared buffers;
//     in a real DAW: file paths relative to the project folder). Audio is never inside JSON.
//
// Why parameters by name and effects by type name?
//   Because indices change when an effect gains a parameter in a later version, names do not.
//   Unknown names are skipped on load (forward compatible), missing ones keep their default.
//
// Numbers: floats are written with %.9g, which is enough digits to read back the identical
// float (9 significant digits round-trip any IEEE-754 single). That is why the self-check can
// demand a bit-identical re-render.
//
// The file has three parts: a minimal JSON value + parser (given in the starter), the writer
// (saveProject) and the reader (loadProject), then the self-check.

#include <dawlab/Common.h>
#include <dawlab/engine/All.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace dawlab;
using namespace dawlab::engine;

namespace {

// =============================================================================================
// Part 1: a minimal JSON value and recursive-descent parser (RFC 8259 subset: no \u escapes
// beyond ASCII, numbers parsed with strtod).
// =============================================================================================
struct Json {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<Json> array;
    std::vector<std::pair<std::string, Json>> object;

    const Json* find(const std::string& key) const {
        if (type != Type::Object)
            return nullptr;
        for (const auto& kv : object)
            if (kv.first == key)
                return &kv.second;
        return nullptr;
    }
    double num(const std::string& key, double def) const {
        const Json* v = find(key);
        return v && v->type == Type::Number ? v->number : def;
    }
    bool flag(const std::string& key, bool def) const {
        const Json* v = find(key);
        return v && v->type == Type::Bool ? v->boolean : def;
    }
    std::string str(const std::string& key, const std::string& def) const {
        const Json* v = find(key);
        return v && v->type == Type::String ? v->string : def;
    }
};

class JsonParser {
public:
    explicit JsonParser(const std::string& text) : p_(text.c_str()), end_(text.c_str() + text.size()) {}

    bool parse(Json& out, std::string* error) {
        const bool ok = value(out) && (skipSpace(), p_ == end_);
        if (!ok && error)
            *error = "JSON syntax error near: " + std::string(p_, std::min<size_t>(20, static_cast<size_t>(end_ - p_)));
        return ok;
    }

private:
    void skipSpace() {
        while (p_ < end_ && std::isspace(static_cast<unsigned char>(*p_)))
            ++p_;
    }
    bool literal(const char* word) {
        const size_t n = std::char_traits<char>::length(word);
        if (static_cast<size_t>(end_ - p_) < n || std::string(p_, n) != word)
            return false;
        p_ += n;
        return true;
    }
    bool stringValue(std::string& out) {
        if (p_ >= end_ || *p_ != '"')
            return false;
        ++p_;
        while (p_ < end_ && *p_ != '"') {
            char c = *p_++;
            if (c == '\\') {
                if (p_ >= end_)
                    return false;
                const char e = *p_++;
                switch (e) {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'r': c = '\r'; break;
                case 'b': c = '\b'; break;
                case 'f': c = '\f'; break;
                case '"': case '\\': case '/': c = e; break;
                default: return false;
                }
            }
            out.push_back(c);
        }
        if (p_ >= end_)
            return false;
        ++p_; // closing quote
        return true;
    }
    bool value(Json& v) {
        skipSpace();
        if (p_ >= end_)
            return false;
        const char c = *p_;
        if (c == '{') {
            ++p_;
            v.type = Json::Type::Object;
            skipSpace();
            if (p_ < end_ && *p_ == '}') {
                ++p_;
                return true;
            }
            for (;;) {
                skipSpace();
                std::string key;
                if (!stringValue(key))
                    return false;
                skipSpace();
                if (p_ >= end_ || *p_++ != ':')
                    return false;
                Json child;
                if (!value(child))
                    return false;
                v.object.emplace_back(std::move(key), std::move(child));
                skipSpace();
                if (p_ < end_ && *p_ == ',') {
                    ++p_;
                    continue;
                }
                if (p_ < end_ && *p_ == '}') {
                    ++p_;
                    return true;
                }
                return false;
            }
        }
        if (c == '[') {
            ++p_;
            v.type = Json::Type::Array;
            skipSpace();
            if (p_ < end_ && *p_ == ']') {
                ++p_;
                return true;
            }
            for (;;) {
                Json child;
                if (!value(child))
                    return false;
                v.array.push_back(std::move(child));
                skipSpace();
                if (p_ < end_ && *p_ == ',') {
                    ++p_;
                    continue;
                }
                if (p_ < end_ && *p_ == ']') {
                    ++p_;
                    return true;
                }
                return false;
            }
        }
        if (c == '"') {
            v.type = Json::Type::String;
            return stringValue(v.string);
        }
        if (literal("true")) {
            v.type = Json::Type::Bool;
            v.boolean = true;
            return true;
        }
        if (literal("false")) {
            v.type = Json::Type::Bool;
            return true;
        }
        if (literal("null"))
            return true;
        // number
        char* after = nullptr;
        const std::string rest(p_, std::min<size_t>(64, static_cast<size_t>(end_ - p_)));
        const double d = std::strtod(rest.c_str(), &after);
        if (after == rest.c_str())
            return false;
        v.type = Json::Type::Number;
        v.number = d;
        p_ += after - rest.c_str();
        return true;
    }

    const char* p_;
    const char* end_;
};

bool parseJson(const std::string& text, Json& out, std::string* error = nullptr) {
    return JsonParser(text).parse(out, error);
}

// The media pool: id -> audio. Clips point at shared buffers; the file stores the id.
using MediaPool = std::map<std::string, std::shared_ptr<const AudioBuffer>>;

const char* lawToString(dsp::PanLaw law) {
    switch (law) {
    case dsp::PanLaw::Linear: return "Linear";
    case dsp::PanLaw::ConstantPower: return "ConstantPower";
    case dsp::PanLaw::Minus4_5dB: return "Minus4_5dB";
    }
    return "ConstantPower";
}
dsp::PanLaw lawFromString(const std::string& s) {
    if (s == "Linear")
        return dsp::PanLaw::Linear;
    if (s == "Minus4_5dB")
        return dsp::PanLaw::Minus4_5dB;
    return dsp::PanLaw::ConstantPower;
}

// =============================================================================================
// Part 2: the writer
// =============================================================================================
std::string jsonString(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\t': out += "\\t"; break;
        default: out += c;
        }
    }
    return out + "\"";
}
std::string num(double v) { // %.9g: enough digits for any float to round-trip
    char b[40];
    std::snprintf(b, sizeof(b), "%.9g", v);
    return b;
}
std::string num(int64_t v) { return std::to_string(v); }
std::string boolean(bool v) { return v ? "true" : "false"; }

std::string saveStrip(const Engine& e, ChannelRef ref, const MediaPool& pool, const std::string& ind) {
    const ChannelStrip& s = *e.getMixer().getStrip(ref);
    const char* kind = ref.kind == ChannelKind::Track ? "track" : (ref.kind == ChannelKind::Bus ? "bus" : "master");
    std::string j = ind + "{\n";
    const std::string in = ind + "  ";
    j += in + "\"kind\": " + jsonString(kind) + ", \"index\": " + std::to_string(ref.index) + ", \"name\": " +
         jsonString(s.getName()) + ",\n";
    j += in + "\"gainDb\": " + num(s.getGainDb()) + ", \"pan\": " + num(s.getPan()) + ", \"panLaw\": " +
         jsonString(lawToString(s.getPanLaw())) + ", \"panMode\": " +
         jsonString(s.getPanMode() == PanMode::Balance ? "Balance" : "PanLaw") + ",\n";
    j += in + "\"mute\": " + boolean(s.isMuted()) + ", \"solo\": " + boolean(s.isSoloed()) + ",\n";
    j += in + "\"sends\": [";
    for (int b = 0; b < kNumBuses; ++b)
        j += std::string(b ? ", " : "") + "{\"levelDb\": " + num(s.getSendLevelDb(b)) + ", \"preFader\": " +
             boolean(s.isSendPreFader(b)) + "}";
    j += "],\n";
    j += in + "\"inserts\": [";
    for (int i = 0; i < e.getNumInserts(ref); ++i) {
        const InsertEffect* fx = e.getInsert(ref, i);
        j += std::string(i ? "," : "") + "\n" + in + "  {\"type\": " + jsonString(fx->getName()) +
             ", \"bypassed\": " + boolean(fx->isBypassed()) + ", \"params\": {";
        for (int p = 0; p < fx->getNumParameters(); ++p)
            j += std::string(p ? ", " : "") + jsonString(fx->getParameterInfo(p).name) + ": " + num(fx->getParameter(p));
        j += "}}";
    }
    j += e.getNumInserts(ref) ? "\n" + in + "]" : "]";
    if (ref.kind == ChannelKind::Track) {
        j += ",\n" + in + "\"clips\": [";
        const std::vector<Clip>* clips = e.getMixer().getTrack(ref.index).getClips();
        const int n = clips ? static_cast<int>(clips->size()) : 0;
        for (int c = 0; c < n; ++c) {
            const Clip& clip = (*clips)[static_cast<size_t>(c)];
            std::string id;
            for (const auto& kv : pool) // reverse lookup: which pool entry is this buffer?
                if (kv.second == clip.source)
                    id = kv.first;
            j += std::string(c ? "," : "") + "\n" + in + "  {\"source\": " + jsonString(id) + ", \"name\": " +
                 jsonString(clip.name) + ", \"start\": " + num(clip.start) + ", \"length\": " + num(clip.length) +
                 ", \"offset\": " + num(clip.sourceOffset) + ", \"gainDb\": " + num(clip.gainDb) +
                 ", \"fadeIn\": " + std::to_string(clip.fadeInSamples) + ", \"fadeOut\": " +
                 std::to_string(clip.fadeOutSamples) + ", \"fadeShape\": " +
                 jsonString(clip.fadeShape == FadeShape::EqualPower ? "EqualPower" : "Linear") + ", \"muted\": " +
                 boolean(clip.muted) + "}";
        }
        j += n ? "\n" + in + "]" : "]";
    }
    return j + "\n" + ind + "}";
}

std::string saveProject(const Engine& e, const MediaPool& pool) {
    const Transport& t = e.getTransport();
    std::string j = "{\n";
    j += "  \"format\": \"dawlab-project\", \"version\": 1,\n";
    j += "  \"sampleRate\": " + num(e.getSampleRate()) + ",\n";
    j += "  \"transport\": {\"tempo\": " + num(t.getTempo()) + ", \"numerator\": " +
         std::to_string(t.getTimeSignature().numerator) + ", \"denominator\": " +
         std::to_string(t.getTimeSignature().denominator) + ", \"loopEnabled\": " + boolean(t.isLoopEnabled()) +
         ", \"loopStart\": " + num(t.getLoopStart()) + ", \"loopEnd\": " + num(t.getLoopEnd()) + "},\n";
    j += "  \"pdc\": " + boolean(e.getMixer().isLatencyCompensationEnabled()) + ",\n";
    j += "  \"strips\": [\n";
    std::vector<ChannelRef> refs;
    for (int i = 0; i < kNumTracks; ++i)
        refs.push_back(ChannelRef::track(i));
    for (int i = 0; i < kNumBuses; ++i)
        refs.push_back(ChannelRef::bus(i));
    refs.push_back(ChannelRef::master());
    for (size_t i = 0; i < refs.size(); ++i)
        j += saveStrip(e, refs[i], pool, "    ") + (i + 1 < refs.size() ? ",\n" : "\n");
    return j + "  ]\n}\n";
}

// =============================================================================================
// Part 3: the reader. `e` must be prepared and empty (a fresh Engine).
// =============================================================================================
bool loadProject(Engine& e, const std::string& text, const MediaPool& pool, std::string* error) {
    auto fail = [&](const std::string& why) {
        if (error)
            *error = why;
        return false;
    };
    Json root;
    if (!parseJson(text, root, error))
        return false;
    if (root.str("format", "") != "dawlab-project")
        return fail("not a dawlab project");
    if (root.num("version", 0) > 1)
        return fail("project was written by a newer version");

    Transport& t = e.getTransport();
    if (const Json* tr = root.find("transport")) {
        t.setTempo(tr->num("tempo", 120.0));
        t.setTimeSignature(static_cast<int>(tr->num("numerator", 4)), static_cast<int>(tr->num("denominator", 4)));
        t.setLoopRange(static_cast<int64_t>(tr->num("loopStart", 0)), static_cast<int64_t>(tr->num("loopEnd", 0)));
        t.setLoopEnabled(tr->flag("loopEnabled", false));
    }
    e.getMixer().setLatencyCompensationEnabled(root.flag("pdc", true));

    const Json* strips = root.find("strips");
    if (!strips || strips->type != Json::Type::Array)
        return fail("missing strips");
    for (const Json& sj : strips->array) {
        const std::string kind = sj.str("kind", "");
        const int index = static_cast<int>(sj.num("index", 0));
        const ChannelRef ref = kind == "track" ? ChannelRef::track(index)
                               : kind == "bus" ? ChannelRef::bus(index)
                                               : ChannelRef::master();
        ChannelStrip* s = e.getMixer().getStrip(ref);
        if (s == nullptr || (kind != "track" && kind != "bus" && kind != "master"))
            return fail("bad strip " + kind + " " + std::to_string(index));
        s->setName(sj.str("name", s->getName()));
        s->setGainDb(static_cast<float>(sj.num("gainDb", 0.0)));
        s->setPan(static_cast<float>(sj.num("pan", 0.0)));
        s->setPanLaw(lawFromString(sj.str("panLaw", "ConstantPower")));
        s->setPanMode(sj.str("panMode", "PanLaw") == "Balance" ? PanMode::Balance : PanMode::PanLaw);
        s->setMute(sj.flag("mute", false));
        s->setSolo(sj.flag("solo", false));
        if (const Json* sends = sj.find("sends"))
            for (int b = 0; b < kNumBuses && b < static_cast<int>(sends->array.size()); ++b) {
                const Json& sd = sends->array[static_cast<size_t>(b)];
                s->setSendLevelDb(b, static_cast<float>(sd.num("levelDb", kMinusInfinityDb)));
                s->setSendPreFader(b, sd.flag("preFader", false));
            }
        if (const Json* inserts = sj.find("inserts"))
            for (const Json& ij : inserts->array) {
                std::unique_ptr<InsertEffect> fx = createEffect(ij.str("type", "").c_str());
                if (!fx)
                    return fail("unknown effect type '" + ij.str("type", "") + "'");
                if (const Json* params = ij.find("params"))
                    for (const auto& kv : params->object)
                        if (kv.second.type == Json::Type::Number) // unknown names are skipped
                            fx->setParameterByName(kv.first.c_str(), static_cast<float>(kv.second.number));
                fx->setBypassed(ij.flag("bypassed", false));
                e.addInsert(ref, -1, std::move(fx));
            }
        if (ref.kind == ChannelKind::Track)
            if (const Json* clips = sj.find("clips")) {
                std::vector<Clip> list;
                for (const Json& cj : clips->array) {
                    const auto it = pool.find(cj.str("source", ""));
                    if (it == pool.end())
                        return fail("missing media '" + cj.str("source", "") + "'");
                    Clip c;
                    c.source = it->second;
                    c.name = cj.str("name", "");
                    c.start = static_cast<int64_t>(cj.num("start", 0));
                    c.length = static_cast<int64_t>(cj.num("length", 0));
                    c.sourceOffset = static_cast<int64_t>(cj.num("offset", 0));
                    c.gainDb = static_cast<float>(cj.num("gainDb", 0));
                    c.fadeInSamples = static_cast<int>(cj.num("fadeIn", 0));
                    c.fadeOutSamples = static_cast<int>(cj.num("fadeOut", 0));
                    c.fadeShape = cj.str("fadeShape", "Linear") == "EqualPower" ? FadeShape::EqualPower : FadeShape::Linear;
                    c.muted = cj.flag("muted", false);
                    list.push_back(std::move(c));
                }
                e.setTrackClips(index, std::move(list));
            }
    }
    return true;
}

// =============================================================================================
// Self-check
// =============================================================================================
constexpr double kSampleRate = 48000.0;

MediaPool makePool() {
    MediaPool pool;
    pool["drums"] = std::make_shared<const AudioBuffer>(makeDrumLoop(kSampleRate, 1, 120.0, 0.7f));
    pool["arp"] = std::make_shared<const AudioBuffer>(makePluckSequence(kSampleRate, 2.0, 0.4f));
    pool["bass A"] = std::make_shared<const AudioBuffer>(makePluck(55.0, kSampleRate, 1.0, 0.5f, 1));
    return pool;
}

void buildProject(Engine& e, const MediaPool& pool) {
    Transport& t = e.getTransport();
    t.setTempo(124.0);
    t.setLoopRange(0, 96000);
    Mixer& m = e.getMixer();
    m.getTrack(0).setName("Drums \"live\""); // a quote, to exercise escaping
    e.setTrackClips(0, {Clip::fromBuffer(pool.at("drums"), 0, "loop 1"), Clip::fromBuffer(pool.at("drums"), 96000, "loop 2")});
    Clip arp = Clip::fromBuffer(pool.at("arp"), 12000, "arp");
    arp.gainDb = -3.5f;
    arp.fadeInSamples = 480;
    arp.fadeOutSamples = 4800;
    arp.fadeShape = FadeShape::EqualPower;
    e.setTrackClips(1, {arp});
    Clip bass = Clip::fromBuffer(pool.at("bass A"), 24000, "bass");
    bass.sourceOffset = 100;
    bass.length = 30000;
    e.setTrackClips(2, {bass});

    m.getTrack(0).setGainDb(-2.5f);
    m.getTrack(1).setPan(-0.3f);
    m.getTrack(1).setPanLaw(dsp::PanLaw::Minus4_5dB);
    m.getTrack(1).setSendLevelDb(0, -9.0f);
    m.getTrack(1).setSendLevelDb(1, -12.0f);
    m.getTrack(1).setSendPreFader(1, true);
    m.getTrack(3).setMute(true);
    m.getBus(0).setGainDb(-1.0f);

    auto comp = createEffect(EffectType::Compressor);
    comp->setParameterByName("Threshold", -21.25f);
    comp->setParameterByName("Ratio", 3.3f);
    e.addInsert(ChannelRef::track(0), -1, std::move(comp));
    auto eq = std::make_unique<ParametricEqEffect>();
    eq->setBand(2, true, dsp::EqBandType::Peak, 1234.5f, 1.7f, 4.2f);
    e.addInsert(ChannelRef::track(1), -1, std::move(eq));
    auto sat = createEffect(EffectType::Saturation);
    sat->setBypassed(true);
    e.addInsert(ChannelRef::track(2), -1, std::move(sat));
    e.addInsert(ChannelRef::bus(0), -1, createEffect(EffectType::Reverb));
    auto delay = createEffect(EffectType::Delay);
    delay->setParameterByName("Time", DelayEffect::noteToMs(124.0, 0.75));
    e.addInsert(ChannelRef::bus(1), -1, std::move(delay));
    e.addInsert(ChannelRef::master(), -1, createEffect(EffectType::Limiter));
}

AudioBuffer renderProject(Engine& e) {
    RenderOptions o;
    o.numSamples = 3 * 48000;
    o.tailSeconds = 1.0;
    return renderOffline(e, o);
}

} // namespace

int main() {
    // ---- the parser on its own ----
    Json j;
    DAWLAB_CHECK(parseJson("{\"a\": [1, 2.5, {\"b\": null}], \"s\": \"x\\\"y\\n\", \"t\": true}", j));
    DAWLAB_CHECK(j.find("a") && j.find("a")->array.size() == 3);
    DAWLAB_CHECK_NEAR(j.find("a")->array[1].number, 2.5, 1e-12);
    DAWLAB_CHECK(j.str("s", "") == "x\"y\n");
    DAWLAB_CHECK(j.flag("t", false));
    Json bad;
    DAWLAB_CHECK(!parseJson("{\"a\": }", bad));
    DAWLAB_CHECK(!parseJson("[1, 2", bad));
    DAWLAB_CHECK(!parseJson("{} trailing", bad));

    // ---- save -> load -> save ----
    const MediaPool pool = makePool();
    Engine original;
    original.prepare(kSampleRate, 512);
    buildProject(original, pool);
    original.applyPendingCommandsOffline();
    const std::string json1 = saveProject(original, pool);

    Engine loaded;
    loaded.prepare(kSampleRate, 512);
    std::string error;
    const bool ok = loadProject(loaded, json1, pool, &error);
    if (!ok)
        std::printf("  load error: %s\n", error.c_str());
    DAWLAB_CHECK(ok);
    loaded.applyPendingCommandsOffline();
    const std::string json2 = saveProject(loaded, pool);
    DAWLAB_CHECK(json1 == json2); // the round trip is lossless
    DAWLAB_CHECK(json1.find("\"Compressor\"") != std::string::npos); // inserts by type name
    DAWLAB_CHECK(json1.find("\"Threshold\": -21.25") != std::string::npos); // parameters by name
    DAWLAB_CHECK(json1.find("\"source\": \"arp\"") != std::string::npos); // clips by media id

    {
        std::ofstream f(outputPath("l14_ex4_project.json"));
        f << json1;
    }
    std::printf("Exercise 4: project saved (%zu bytes) to dawlab_output/l14_ex4_project.json\n", json1.size());
    std::printf("%.600s...\n", json1.c_str());

    // ---- the loaded project sounds identical ----
    const AudioBuffer a = renderProject(original);
    const AudioBuffer b = renderProject(loaded);
    float diff = 0.0f;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < a.getNumSamples(); ++i)
            diff = std::max(diff, std::abs(a.getSample(ch, i) - b.getSample(ch, i)));
    std::printf("  re-render of the loaded project: max |difference| = %.3g (peak %.2f dBFS)\n",
                static_cast<double>(diff), static_cast<double>(gainToDb(peakAbs(a))));
    DAWLAB_CHECK(a.getNumSamples() == b.getNumSamples());
    DAWLAB_CHECK(diff == 0.0f);
    DAWLAB_CHECK(peakAbs(a) > 0.01f);

    // ---- errors are reported, not crashed on ----
    std::string bad1 = json1;
    const size_t at = bad1.find("\"Compressor\"");
    if (at != std::string::npos)
        bad1.replace(at, 12, "\"Compresor\"");
    Engine e3;
    e3.prepare(kSampleRate, 512);
    DAWLAB_CHECK(!loadProject(e3, bad1, pool, &error));
    std::printf("  misspelled effect -> error: %s\n", error.c_str());
    MediaPool small = pool;
    small.erase("arp");
    Engine e4;
    e4.prepare(kSampleRate, 512);
    DAWLAB_CHECK(!loadProject(e4, json1, small, &error));
    std::printf("  missing media     -> error: %s\n", error.c_str());
    return dawlab::test::summary("l14_ex4");
}
