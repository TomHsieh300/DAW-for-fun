// l14_ex11_solution -- Exercise 11: save / open a project from the File menu (solution).
//
// What it shows
//   - ProjectFile: the whole Session as XML (juce::ValueTree -> juce::XmlElement): transport,
//     every strip (name, fader, pan, mute, solo, sends, inserts with every parameter BY NAME,
//     bypass, VST3 file + state as base64), every clip. Audio is not inside the XML: each distinct
//     source buffer is written once as a 32-bit float WAV into "<project>_media/" and the clips
//     refer to it by id. 32-bit float keeps every sample bit-exact; floats in the XML are written
//     with 9 significant digits, enough for any float to survive the round trip.
//   - ProjectCommands: an ApplicationCommandTarget with "Save project..." (Ctrl+S) and
//     "Open project..." (Ctrl+Shift+O). MainComponent::setExtraCommandTarget() puts it behind the
//     app's own target, onExtendMenu adds the items to the File menu - the app itself is not
//     modified.
//
// Run
//   build/bin/Debug/l14_ex11_solution.exe
//
// Output
//   dawlab_output/l14_ex11_project.dawlab (+ l14_ex11_project_media/*.wav)
//   dawlab_output/l14_ex11_copy/l14_ex11_project.dawlab (saved again after loading)
//   Self-check: menu item and shortcut present, a re-render after save + open is bit-identical,
//   and saving the loaded project gives the identical XML text.

#include <dawlab/Common.h>
#include <dawlab/engine/All.h>

#include "EffectMenu.h"
#include "Exporter.h"
#include "MainComponent.h"
#include "Session.h"
#include "VstInsert.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdio>
#include <cstdlib>
#include <map>

using namespace dawlab;
using namespace dawlab::engine;
using namespace dawlab::app;

namespace ex11 {

// Floats as text: 9 significant digits round-trip every float exactly.
juce::String f2s(float v) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.9g", static_cast<double>(v));
    return buffer;
}
float s2f(const juce::var& v) { return std::strtof(v.toString().toRawUTF8(), nullptr); }
int64_t s2i(const juce::var& v) { return v.toString().getLargeIntValue(); }

class ProjectFile {
public:
    static constexpr int kVersion = 1;

    static bool save(Session& session, const juce::File& file, juce::String& error) {
        const juce::File mediaDir = file.getSiblingFile(file.getFileNameWithoutExtension() + "_media");
        mediaDir.deleteRecursively();
        if (!mediaDir.createDirectory()) {
            error = "Cannot create " + mediaDir.getFullPathName();
            return false;
        }
        Engine& engine = session.getEngine();
        const Transport& tr = engine.getTransport();
        juce::ValueTree root("DawLabProject");
        root.setProperty("version", kVersion, nullptr);
        root.setProperty("sampleRate", engine.getSampleRate(), nullptr);
        root.setProperty("tempo", tr.getTempo(), nullptr);
        root.setProperty("numerator", tr.getTimeSignature().numerator, nullptr);
        root.setProperty("denominator", tr.getTimeSignature().denominator, nullptr);
        root.setProperty("loopStart", juce::String(tr.getLoopStart()), nullptr);
        root.setProperty("loopEnd", juce::String(tr.getLoopEnd()), nullptr);
        root.setProperty("loopOn", tr.isLoopEnabled(), nullptr);

        // media: one WAV per distinct buffer, in order of first use
        std::map<const AudioBuffer*, juce::String> ids;
        for (int t = 0; t < kNumTracks; ++t)
            for (const Clip& c : session.getClips(t)) {
                if (!c.source || ids.count(c.source.get()))
                    continue;
                const juce::String id = "m" + juce::String(static_cast<int>(ids.size()));
                const juce::File wav = mediaDir.getChildFile(id + ".wav");
                if (!Exporter::writeWavFile(wav, *c.source, engine.getSampleRate(), 32, error))
                    return false;
                ids[c.source.get()] = id;
                juce::ValueTree media("Media");
                media.setProperty("id", id, nullptr);
                media.setProperty("file", mediaDir.getFileName() + "/" + wav.getFileName(), nullptr);
                root.appendChild(media, nullptr);
            }

        for (int s = 0; s < kNumStrips; ++s)
            root.appendChild(stripToTree(session, stripRefAt(s), ids), nullptr);

        std::unique_ptr<juce::XmlElement> xml = root.createXml();
        if (!xml->writeTo(file)) {
            error = "Cannot write " + file.getFullPathName();
            return false;
        }
        return true;
    }

    static bool load(Session& session, host::PluginHost* pluginHost, const juce::File& file, juce::String& error) {
        std::unique_ptr<juce::XmlElement> xml = juce::XmlDocument::parse(file);
        if (xml == nullptr || !xml->hasTagName("DawLabProject")) {
            error = "Not a DawLab project: " + file.getFullPathName();
            return false;
        }
        const juce::ValueTree root = juce::ValueTree::fromXml(*xml);
        if (static_cast<int>(root["version"]) > kVersion) {
            error = "The project was saved by a newer version";
            return false;
        }
        Engine& engine = session.getEngine();
        const double fileRate = root["sampleRate"];

        // media first: if one file is missing, nothing is changed
        std::map<juce::String, std::shared_ptr<const AudioBuffer>> media;
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        for (const auto& m : root) {
            if (!m.hasType("Media"))
                continue;
            const juce::File wav = file.getParentDirectory().getChildFile(m["file"].toString());
            std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(wav));
            if (reader == nullptr) {
                error = "Missing media file " + wav.getFullPathName();
                return false;
            }
            auto buffer = std::make_shared<AudioBuffer>(static_cast<int>(reader->numChannels),
                                                        static_cast<int>(reader->lengthInSamples));
            juce::AudioBuffer<float> view(buffer->getArrayOfWritePointers(), buffer->getNumChannels(),
                                          buffer->getNumSamples());
            reader->read(&view, 0, buffer->getNumSamples(), 0, true, true);
            std::shared_ptr<const AudioBuffer> audio = buffer;
            if (fileRate != engine.getSampleRate()) // saved at another rate: convert like a device change
                audio = std::make_shared<const AudioBuffer>(resampleBuffer(*audio, fileRate, engine.getSampleRate()));
            media[m["id"].toString()] = audio;
        }

        session.newProject();
        for (int s = 0; s < kNumStrips; ++s)
            for (int slot = 0; slot < kInsertSlotsPerStrip; ++slot)
                session.clearInsert(stripRefAt(s), slot);

        Transport& tr = engine.getTransport();
        tr.setTempo(root["tempo"]);
        tr.setTimeSignature(root["numerator"], root["denominator"]);
        tr.setLoopRange(s2i(root["loopStart"]), s2i(root["loopEnd"]));
        tr.setLoopEnabled(root["loopOn"]);

        int s = 0;
        for (const auto& strip : root) {
            if (!strip.hasType("Strip"))
                continue;
            if (!stripFromTree(session, pluginHost, stripRefAt(s++), strip, media, error))
                return false;
        }
        if (fileRate != engine.getSampleRate()) // positions were saved in samples at fileRate
            session.handleSampleRateChange(fileRate, engine.getSampleRate());
        return true;
    }

private:
    static juce::ValueTree stripToTree(Session& session, ChannelRef ref, const std::map<const AudioBuffer*, juce::String>& ids) {
        ChannelStrip& cs = *session.getEngine().getMixer().getStrip(ref);
        juce::ValueTree strip("Strip");
        strip.setProperty("name", juce::String(cs.getName()), nullptr);
        strip.setProperty("gainDb", f2s(cs.getGainDb()), nullptr);
        strip.setProperty("pan", f2s(cs.getPan()), nullptr);
        strip.setProperty("panLaw", static_cast<int>(cs.getPanLaw()), nullptr);
        strip.setProperty("panMode", static_cast<int>(cs.getPanMode()), nullptr);
        strip.setProperty("mute", cs.isMuted(), nullptr);
        strip.setProperty("solo", cs.isSoloed(), nullptr);
        if (ref.kind == ChannelKind::Track)
            for (int b = 0; b < kNumBuses; ++b) {
                juce::ValueTree send("Send");
                send.setProperty("db", f2s(cs.getSendLevelDb(b)), nullptr);
                send.setProperty("pre", cs.isSendPreFader(b), nullptr);
                strip.appendChild(send, nullptr);
            }
        for (int slot = 0; slot < kInsertSlotsPerStrip; ++slot) {
            const InsertSlotState& st = session.getSlot(ref, slot);
            if (st.isEmpty())
                continue;
            juce::ValueTree insert("Insert");
            insert.setProperty("slot", slot, nullptr);
            insert.setProperty("bypass", st.effect->isBypassed(), nullptr);
            if (st.kind == SlotKind::Vst3) {
                insert.setProperty("kind", "vst3", nullptr);
                const auto& desc = st.vst->getPlugin().getInstance().getPluginDescription();
                insert.setProperty("file", desc.fileOrIdentifier, nullptr);
                insert.setProperty("state", st.vst->getPlugin().getState().toBase64Encoding(), nullptr);
            } else {
                insert.setProperty("kind", "builtin", nullptr);
                insert.setProperty("type", st.effect->getName(), nullptr);
                for (int i = 0; i < st.effect->getNumParameters(); ++i) {
                    juce::ValueTree p("Param");
                    p.setProperty("name", juce::String(st.effect->getParameterInfo(i).name), nullptr);
                    p.setProperty("value", f2s(st.effect->getParameter(i)), nullptr);
                    insert.appendChild(p, nullptr);
                }
            }
            strip.appendChild(insert, nullptr);
        }
        if (ref.kind == ChannelKind::Track)
            for (const Clip& c : session.getClips(ref.index)) {
                juce::ValueTree clip("Clip");
                clip.setProperty("media", c.source ? ids.at(c.source.get()) : juce::String(), nullptr);
                clip.setProperty("name", juce::String(c.name), nullptr);
                clip.setProperty("start", juce::String(c.start), nullptr);
                clip.setProperty("length", juce::String(c.length), nullptr);
                clip.setProperty("offset", juce::String(c.sourceOffset), nullptr);
                clip.setProperty("gainDb", f2s(c.gainDb), nullptr);
                clip.setProperty("fadeIn", c.fadeInSamples, nullptr);
                clip.setProperty("fadeOut", c.fadeOutSamples, nullptr);
                clip.setProperty("shape", c.fadeShape == FadeShape::EqualPower ? "equalPower" : "linear", nullptr);
                clip.setProperty("muted", c.muted, nullptr);
                strip.appendChild(clip, nullptr);
            }
        return strip;
    }

    static bool stripFromTree(Session& session, host::PluginHost* pluginHost, ChannelRef ref, const juce::ValueTree& strip,
                              const std::map<juce::String, std::shared_ptr<const AudioBuffer>>& media, juce::String& error) {
        Engine& engine = session.getEngine();
        ChannelStrip& cs = *engine.getMixer().getStrip(ref);
        session.setStripName(ref, strip["name"].toString());
        cs.setGainDb(s2f(strip["gainDb"]));
        cs.setPan(s2f(strip["pan"]));
        cs.setPanLaw(static_cast<dsp::PanLaw>(static_cast<int>(strip["panLaw"])));
        cs.setPanMode(static_cast<PanMode>(static_cast<int>(strip["panMode"])));
        cs.setMute(strip["mute"]);
        cs.setSolo(strip["solo"]);
        int bus = 0;
        std::vector<Clip> clips;
        for (const auto& child : strip) {
            if (child.hasType("Send") && bus < kNumBuses) {
                cs.setSendLevelDb(bus, s2f(child["db"]));
                cs.setSendPreFader(bus, child["pre"]);
                ++bus;
            } else if (child.hasType("Insert")) {
                const int slot = child["slot"];
                InsertEffect* fx = nullptr;
                if (child["kind"].toString() == "vst3") {
                    if (pluginHost == nullptr)
                        continue;
                    juce::String why;
                    auto instance = pluginHost->loadFromFile(juce::File(child["file"].toString()), engine.getSampleRate(),
                                                             engine.getMaxBlockSize(), why);
                    if (instance == nullptr) {
                        error = "Plugin " + child["file"].toString() + ": " + why;
                        return false;
                    }
                    auto hosted = std::make_unique<host::HostedPlugin>(std::move(instance));
                    juce::MemoryBlock state;
                    state.fromBase64Encoding(child["state"].toString());
                    hosted->setState(state);
                    fx = session.setVstInsert(ref, slot, std::make_unique<VstInsert>(std::move(hosted)));
                } else {
                    EffectType type;
                    if (!effectTypeFromName(child["type"].toString().toRawUTF8(), type)) {
                        error = "Unknown effect type " + child["type"].toString();
                        return false;
                    }
                    fx = session.setBuiltinInsert(ref, slot, type);
                    if (fx != nullptr)
                        for (const auto& p : child)
                            fx->setParameterByName(p["name"].toString().toRawUTF8(), s2f(p["value"])); // unknown names: skipped
                }
                if (fx != nullptr && static_cast<bool>(child["bypass"]))
                    session.setInsertBypassed(ref, slot, true);
            } else if (child.hasType("Clip")) {
                auto it = media.find(child["media"].toString());
                if (it == media.end()) {
                    error = "Clip refers to unknown media " + child["media"].toString();
                    return false;
                }
                Clip c;
                c.source = it->second;
                c.name = child["name"].toString().toStdString();
                c.start = s2i(child["start"]);
                c.length = s2i(child["length"]);
                c.sourceOffset = s2i(child["offset"]);
                c.gainDb = s2f(child["gainDb"]);
                c.fadeInSamples = child["fadeIn"];
                c.fadeOutSamples = child["fadeOut"];
                c.fadeShape = child["shape"].toString() == "equalPower" ? FadeShape::EqualPower : FadeShape::Linear;
                c.muted = child["muted"];
                clips.push_back(std::move(c));
            }
        }
        if (ref.kind == ChannelKind::Track)
            session.setClips(ref.index, std::move(clips));
        return true;
    }
};

// File > Save project... / Open project..., behind MainComponent's own commands.
class ProjectCommands final : public juce::ApplicationCommandTarget {
public:
    enum : juce::CommandID { saveProject = 0x3001, openProject = 0x3002 };

    explicit ProjectCommands(MainComponent& main) : main_(main) {
        main_.setExtraCommandTarget(this);
        main_.onExtendMenu = [this](int menuIndex, juce::PopupMenu& menu) {
            if (menuIndex != 0)
                return;
            juce::PopupMenu withProject; // put our two items right at the top of the File menu
            withProject.addCommandItem(&main_.getCommandManager(), openProject);
            withProject.addCommandItem(&main_.getCommandManager(), saveProject);
            withProject.addSeparator();
            for (juce::PopupMenu::MenuItemIterator it(menu); it.next();)
                withProject.addItem(it.getItem());
            menu = withProject;
        };
    }
    ~ProjectCommands() override {
        main_.onExtendMenu = nullptr;
        main_.setExtraCommandTarget(nullptr);
    }

    juce::ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }
    void getAllCommands(juce::Array<juce::CommandID>& c) override { c.addArray({saveProject, openProject}); }
    void getCommandInfo(juce::CommandID id, juce::ApplicationCommandInfo& info) override {
        if (id == saveProject) {
            info.setInfo("Save project...", "Save the project as XML + media", CommandCategories::file, 0);
            info.addDefaultKeypress('s', juce::ModifierKeys::commandModifier);
        } else if (id == openProject) {
            info.setInfo("Open project...", "Open a saved project", CommandCategories::file, 0);
            info.addDefaultKeypress('o', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier);
        }
    }
    bool perform(const InvocationInfo& info) override {
        const bool save = info.commandID == saveProject;
        if (!save && info.commandID != openProject)
            return false;
        chooser_ = std::make_unique<juce::FileChooser>(save ? "Save project" : "Open project",
                                                       juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
                                                       "*.dawlab");
        const int flags = save ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting
                               : juce::FileBrowserComponent::openMode;
        chooser_->launchAsync(flags | juce::FileBrowserComponent::canSelectFiles, [this, save](const juce::FileChooser& fc) {
            const juce::File f = fc.getResult();
            if (f != juce::File())
                save ? saveTo(f.withFileExtension("dawlab")) : openFrom(f);
        });
        return true;
    }

    bool saveTo(const juce::File& file) {
        juce::String error;
        const bool ok = ProjectFile::save(main_.getSession(), file, error);
        main_.setStatus(ok ? "Saved " + file.getFullPathName() : "Save failed: " + error);
        return ok;
    }
    bool openFrom(const juce::File& file) {
        main_.closeAllEditors();
        juce::String error;
        const bool ok = ProjectFile::load(main_.getSession(), &main_.getPluginHost(), file, error);
        main_.getArrangement().zoomToFit();
        main_.setStatus(ok ? "Opened " + file.getFullPathName() : "Open failed: " + error);
        return ok;
    }

private:
    MainComponent& main_;
    std::unique_ptr<juce::FileChooser> chooser_;
};

} // namespace ex11

namespace {
AudioBuffer renderBars(Engine& engine, int bars) {
    engine.applyPendingCommandsOffline();
    RenderOptions ro;
    ro.numSamples = static_cast<int64_t>(bars * engine.getTransport().getSamplesPerBar());
    ro.tailSeconds = 0.5;
    return renderOffline(engine, ro);
}

bool menuHasCommand(const juce::PopupMenu& menu, juce::CommandID id) {
    for (juce::PopupMenu::MenuItemIterator it(menu); it.next();)
        if (it.getItem().commandManager != nullptr && it.getItem().itemID == id)
            return true;
    return false;
}
} // namespace

int main() {
    const juce::ScopedJuceInitialiser_GUI juceInit;
    using namespace ex11;
    const juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile("dawlab_output");
    const juce::File projectFile = out.getChildFile("l14_ex11_project.dawlab");
    const juce::File copyFile = out.getChildFile("l14_ex11_copy").getChildFile("l14_ex11_project.dawlab");
    copyFile.getParentDirectory().createDirectory();

    AudioBuffer before, after;
    {
        MainComponent main(MainComponent::Mode::Offline);
        ProjectCommands commands(main);
        DAWLAB_CHECK(main.loadDemoSongNow() == 0);
        // make the project a bit "used": tempo, loop, a moved clip, a bypassed insert, a mute
        Session& s = main.getSession();
        main.getEngine().getTransport().setTempo(118.0);
        main.getEngine().getTransport().setLoopRangeBars(2, 2);
        main.getEngine().getTransport().setLoopEnabled(true);
        s.moveClip(5, 0, s.getClips(5)[0].start - 12000);
        s.setInsertBypassed(ChannelRef::track(2), 1, true);
        main.getEngine().getMixer().getTrack(6).setMute(true);
        s.getSlot(ChannelRef::track(1), 0).effect->setParameterByName("B3 Gain", 4.5f);

        // the File menu and the shortcut
        const juce::PopupMenu fileMenu = main.getMenuForIndex(0, "File");
        DAWLAB_CHECK(menuHasCommand(fileMenu, ProjectCommands::saveProject));
        DAWLAB_CHECK(menuHasCommand(fileMenu, ProjectCommands::openProject));
        const auto keys = main.getCommandManager().getKeyMappings()->getKeyPressesAssignedToCommand(ProjectCommands::saveProject);
        DAWLAB_CHECK(keys.contains(juce::KeyPress('s', juce::ModifierKeys::commandModifier, 0)));

        DAWLAB_CHECK(commands.saveTo(projectFile));
        before = renderBars(main.getEngine(), 4);
    }
    juce::String firstText = projectFile.loadFileAsString();
    std::printf("saved %s (%d bytes of XML, %d media files)\n", projectFile.getFullPathName().toRawUTF8(),
                firstText.length(), projectFile.getSiblingFile("l14_ex11_project_media").getNumberOfChildFiles(juce::File::findFiles));
    {
        MainComponent main(MainComponent::Mode::Offline); // a fresh app: empty project
        ProjectCommands commands(main);
        DAWLAB_CHECK(commands.openFrom(projectFile));
        DAWLAB_CHECK(main.getEngine().getTransport().getTempo() == 118.0);
        DAWLAB_CHECK(main.getEngine().getTransport().isLoopEnabled());
        DAWLAB_CHECK(main.getEngine().getMixer().getTrack(6).isMuted());
        const InsertEffect* phaser = main.getSession().getSlot(ChannelRef::track(2), 1).effect;
        DAWLAB_CHECK(phaser != nullptr && phaser->isBypassed());
        after = renderBars(main.getEngine(), 4);
        DAWLAB_CHECK(commands.saveTo(copyFile));
    }
    float worst = 0.0f;
    const int n = std::min(before.getNumSamples(), after.getNumSamples());
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < n; ++i)
            worst = std::max(worst, std::abs(before.getSample(ch, i) - after.getSample(ch, i)));
    std::printf("re-render after save + open: %d samples, max difference %g\n", n, static_cast<double>(worst));
    DAWLAB_CHECK(before.getNumSamples() == after.getNumSamples());
    DAWLAB_CHECK(worst == 0.0f);
    const bool sameText = copyFile.loadFileAsString() == firstText;
    std::printf("saving the opened project gives %s XML\n", sameText ? "identical" : "DIFFERENT");
    DAWLAB_CHECK(sameText);
    return test::summary("l14_ex11");
}
