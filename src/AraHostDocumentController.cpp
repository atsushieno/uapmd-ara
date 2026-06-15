#include "AraHostDocumentController.hpp"

#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <vector>

namespace uapmd::ara {

    namespace {
        ARA::ARAAssertFunction gAraAssertFunction{};

        struct HostAudioSource {
            ProjectObjectId id;
            std::string name;
            std::string persistent_id;
        };

        struct HostRegionSequence {
            ProjectObjectId id;
            std::string name;
            std::string persistent_id;
        };

        struct HostAudioModification {
            ProjectObjectId id;
            std::string name;
            std::string persistent_id;
        };

        struct HostPlaybackRegion {
            ProjectObjectId id;
            std::string name;
        };

        struct HostAudioReader {
            ProjectObjectId audio_source_id;
            bool use64BitSamples{};
        };

        double secondsFromSamples(int64_t samples, double sampleRate) {
            if (sampleRate <= 0)
                sampleRate = 48000.0;
            return static_cast<double>(samples) / sampleRate;
        }

        ARA::ARABool readAudioSamplesFromImpl(
            AraHostDocumentController::Impl* impl,
            HostAudioReader* reader,
            ARA::ARASamplePosition samplePosition,
            ARA::ARASampleCount samplesPerChannel,
            void* const buffers[]);

        ARA::ARAAudioReaderHostRef ARA_CALL createAudioReaderForSource(
            ARA::ARAAudioAccessControllerHostRef controllerHostRef,
            ARA::ARAAudioSourceHostRef audioSourceHostRef,
            ARA::ARABool use64BitSamples) {
            auto* source = reinterpret_cast<HostAudioSource*>(audioSourceHostRef);
            if (!controllerHostRef || !source)
                return nullptr;
            auto* reader = new HostAudioReader{
                .audio_source_id = source->id,
                .use64BitSamples = use64BitSamples != ARA::kARAFalse
            };
            return reinterpret_cast<ARA::ARAAudioReaderHostRef>(reader);
        }

        ARA::ARABool ARA_CALL readAudioSamples(
            ARA::ARAAudioAccessControllerHostRef controllerHostRef,
            ARA::ARAAudioReaderHostRef audioReaderHostRef,
            ARA::ARASamplePosition samplePosition,
            ARA::ARASampleCount samplesPerChannel,
            void* const buffers[]) {
            return readAudioSamplesFromImpl(
                reinterpret_cast<AraHostDocumentController::Impl*>(controllerHostRef),
                reinterpret_cast<HostAudioReader*>(audioReaderHostRef),
                samplePosition,
                samplesPerChannel,
                buffers);
        }

        void ARA_CALL destroyAudioReader(
            ARA::ARAAudioAccessControllerHostRef controllerHostRef,
            ARA::ARAAudioReaderHostRef audioReaderHostRef) {
            (void) controllerHostRef;
            delete reinterpret_cast<HostAudioReader*>(audioReaderHostRef);
        }

        ARA::ARASize ARA_CALL getArchiveSize(
            ARA::ARAArchivingControllerHostRef controllerHostRef,
            ARA::ARAArchiveReaderHostRef archiveReaderHostRef) {
            (void) controllerHostRef;
            (void) archiveReaderHostRef;
            return 0;
        }

        ARA::ARABool ARA_CALL readBytesFromArchive(
            ARA::ARAArchivingControllerHostRef controllerHostRef,
            ARA::ARAArchiveReaderHostRef archiveReaderHostRef,
            ARA::ARASize position,
            ARA::ARASize length,
            ARA::ARAByte buffer[]) {
            (void) controllerHostRef;
            (void) archiveReaderHostRef;
            (void) position;
            (void) length;
            (void) buffer;
            return ARA::kARAFalse;
        }

        ARA::ARABool ARA_CALL writeBytesToArchive(
            ARA::ARAArchivingControllerHostRef controllerHostRef,
            ARA::ARAArchiveWriterHostRef archiveWriterHostRef,
            ARA::ARASize position,
            ARA::ARASize length,
            const ARA::ARAByte buffer[]) {
            (void) controllerHostRef;
            (void) archiveWriterHostRef;
            (void) position;
            (void) length;
            (void) buffer;
            return ARA::kARAFalse;
        }

        void ARA_CALL notifyDocumentArchivingProgress(
            ARA::ARAArchivingControllerHostRef controllerHostRef,
            float value) {
            (void) controllerHostRef;
            (void) value;
        }

        void ARA_CALL notifyDocumentUnarchivingProgress(
            ARA::ARAArchivingControllerHostRef controllerHostRef,
            float value) {
            (void) controllerHostRef;
            (void) value;
        }

        ARA::ARAPersistentID ARA_CALL getDocumentArchiveID(
            ARA::ARAArchivingControllerHostRef controllerHostRef,
            ARA::ARAArchiveReaderHostRef archiveReaderHostRef) {
            (void) controllerHostRef;
            (void) archiveReaderHostRef;
            return nullptr;
        }
    } // namespace

    struct AraHostDocumentController::Impl {
        const ARA::ARAFactory* factory{};
        std::string document_name;
        ARA::ARAInterfaceConfiguration interface_configuration{};
        ARA::ARAAudioAccessControllerInterface audio_access_interface{};
        ARA::ARAArchivingControllerInterface archiving_interface{};
        ARA::ARADocumentControllerHostInstance host_instance{};
        ARA::ARADocumentProperties document_properties{};
        const ARA::ARADocumentControllerInstance* controller_instance{};
        bool initialized_factory{};
        ProjectDocumentView* document_view{};
        ARA::ARAMusicalContextRef musical_context_ref{};
        HostRegionSequence musical_context_host{};
        std::map<ProjectObjectId, HostRegionSequence> region_sequence_hosts{};
        std::map<ProjectObjectId, ARA::ARARegionSequenceRef> region_sequence_refs{};
        std::map<ProjectObjectId, HostAudioSource> audio_source_hosts{};
        std::map<ProjectObjectId, ARA::ARAAudioSourceRef> audio_source_refs{};
        std::map<ProjectObjectId, HostAudioModification> audio_modification_hosts{};
        std::map<ProjectObjectId, ARA::ARAAudioModificationRef> audio_modification_refs{};
        std::map<ProjectObjectId, HostPlaybackRegion> playback_region_hosts{};
        std::map<ProjectObjectId, ARA::ARAPlaybackRegionRef> playback_region_refs{};

        explicit Impl(const ARA::ARAFactory& factory, std::string documentName)
            : factory(&factory), document_name(std::move(documentName)) {
            const auto apiGeneration = static_cast<ARA::ARAAPIGeneration>(
                std::min(
                    static_cast<int>(factory.highestSupportedApiGeneration),
                    static_cast<int>(ARA::kARAAPIGeneration_2_0_Final)));
            interface_configuration = ARA::ARAInterfaceConfiguration{
                .structSize = ARA::kARAInterfaceConfigurationMinSize,
                .desiredApiGeneration = apiGeneration,
                .assertFunctionAddress = &gAraAssertFunction
            };
            if (factory.initializeARAWithConfiguration) {
                factory.initializeARAWithConfiguration(&interface_configuration);
                initialized_factory = true;
            }

            audio_access_interface = ARA::ARAAudioAccessControllerInterface{
                .structSize = ARA::kARAAudioAccessControllerInterfaceMinSize,
                .createAudioReaderForSource = createAudioReaderForSource,
                .readAudioSamples = readAudioSamples,
                .destroyAudioReader = destroyAudioReader
            };
            archiving_interface = ARA::ARAArchivingControllerInterface{
                .structSize = ARA::kARAArchivingControllerInterfaceMinSize,
                .getArchiveSize = getArchiveSize,
                .readBytesFromArchive = readBytesFromArchive,
                .writeBytesToArchive = writeBytesToArchive,
                .notifyDocumentArchivingProgress = notifyDocumentArchivingProgress,
                .notifyDocumentUnarchivingProgress = notifyDocumentUnarchivingProgress,
                .getDocumentArchiveID = getDocumentArchiveID
            };
            host_instance = ARA::ARADocumentControllerHostInstance{
                .structSize = ARA::kARADocumentControllerHostInstanceMinSize,
                .audioAccessControllerHostRef = reinterpret_cast<ARA::ARAAudioAccessControllerHostRef>(this),
                .audioAccessControllerInterface = &audio_access_interface,
                .archivingControllerHostRef = reinterpret_cast<ARA::ARAArchivingControllerHostRef>(this),
                .archivingControllerInterface = &archiving_interface,
                .contentAccessControllerHostRef = nullptr,
                .contentAccessControllerInterface = nullptr,
                .modelUpdateControllerHostRef = nullptr,
                .modelUpdateControllerInterface = nullptr,
                .playbackControllerHostRef = nullptr,
                .playbackControllerInterface = nullptr
            };
            document_properties = ARA::ARADocumentProperties{
                .structSize = ARA::kARADocumentPropertiesMinSize,
                .name = document_name.empty() ? nullptr : document_name.c_str()
            };

            if (factory.createDocumentControllerWithDocument)
                controller_instance = factory.createDocumentControllerWithDocument(&host_instance, &document_properties);
        }

        ~Impl() {
            if (controller_instance &&
                controller_instance->documentControllerInterface &&
                controller_instance->documentControllerInterface->destroyDocumentController)
                controller_instance->documentControllerInterface->destroyDocumentController(
                    controller_instance->documentControllerRef);
            controller_instance = nullptr;

            if (initialized_factory && factory && factory->uninitializeARA)
                factory->uninitializeARA();
        }

        bool valid() const {
            return controller_instance &&
                controller_instance->documentControllerRef &&
                controller_instance->documentControllerInterface;
        }

        const ARA::ARADocumentControllerInterface* controllerInterface() const {
            if (!valid())
                return nullptr;
            return controller_instance->documentControllerInterface;
        }

        void beginEditing() {
            if (auto* controller = controllerInterface(); controller && controller->beginEditing)
                controller->beginEditing(controller_instance->documentControllerRef);
        }

        void endEditing() {
            if (auto* controller = controllerInterface(); controller && controller->endEditing)
                controller->endEditing(controller_instance->documentControllerRef);
        }

        void notifyModelUpdates() {
            if (auto* controller = controllerInterface(); controller && controller->notifyModelUpdates)
                controller->notifyModelUpdates(controller_instance->documentControllerRef);
        }

        void clearModel() {
            auto* controller = controllerInterface();
            if (!controller)
                return;

            for (auto it = playback_region_refs.rbegin(); it != playback_region_refs.rend(); ++it)
                if (controller->destroyPlaybackRegion)
                    controller->destroyPlaybackRegion(controller_instance->documentControllerRef, it->second);
            playback_region_refs.clear();
            playback_region_hosts.clear();

            for (auto it = audio_modification_refs.rbegin(); it != audio_modification_refs.rend(); ++it)
                if (controller->destroyAudioModification)
                    controller->destroyAudioModification(controller_instance->documentControllerRef, it->second);
            audio_modification_refs.clear();
            audio_modification_hosts.clear();

            for (auto it = audio_source_refs.rbegin(); it != audio_source_refs.rend(); ++it)
                if (controller->destroyAudioSource)
                    controller->destroyAudioSource(controller_instance->documentControllerRef, it->second);
            audio_source_refs.clear();
            audio_source_hosts.clear();

            for (auto it = region_sequence_refs.rbegin(); it != region_sequence_refs.rend(); ++it)
                if (controller->destroyRegionSequence)
                    controller->destroyRegionSequence(controller_instance->documentControllerRef, it->second);
            region_sequence_refs.clear();
            region_sequence_hosts.clear();

            if (musical_context_ref && controller->destroyMusicalContext)
                controller->destroyMusicalContext(controller_instance->documentControllerRef, musical_context_ref);
            musical_context_ref = nullptr;
            musical_context_host = {};
        }

        bool populateModel(ProjectDocumentView& view) {
            auto* controller = controllerInterface();
            if (!controller)
                return false;

            document_view = &view;
            beginEditing();
            clearModel();

            musical_context_host = HostRegionSequence{
                .id = "master",
                .name = "Master",
                .persistent_id = "uapmd.master"
            };
            ARA::ARAMusicalContextProperties musicalContextProperties{
                .structSize = ARA::kARAMusicalContextPropertiesMinSize,
                .name = musical_context_host.name.c_str(),
                .orderIndex = 0,
                .color = nullptr
            };
            if (controller->createMusicalContext)
                musical_context_ref = controller->createMusicalContext(
                    controller_instance->documentControllerRef,
                    reinterpret_cast<ARA::ARAMusicalContextHostRef>(&musical_context_host),
                    &musicalContextProperties);

            for (const auto& trackId : view.trackIds()) {
                auto track = view.getTrack(trackId);
                if (!track || track->masterTrack)
                    continue;

                auto& host = region_sequence_hosts[trackId];
                host = HostRegionSequence{
                    .id = trackId,
                    .name = "Track " + std::to_string(track->trackIndex),
                    .persistent_id = "uapmd.track." + trackId
                };
                ARA::ARARegionSequenceProperties properties{
                    .structSize = ARA::kARARegionSequencePropertiesMinSize,
                    .name = host.name.c_str(),
                    .orderIndex = track->trackIndex,
                    .musicalContextRef = musical_context_ref,
                    .color = nullptr,
                    .persistentID = host.persistent_id.c_str()
                };
                if (controller->createRegionSequence)
                    region_sequence_refs[trackId] = controller->createRegionSequence(
                        controller_instance->documentControllerRef,
                        reinterpret_cast<ARA::ARARegionSequenceHostRef>(&host),
                        &properties);
            }

            for (const auto& audioSourceId : view.audioSourceIds()) {
                auto source = view.getAudioSource(audioSourceId);
                if (!source || source->frameCount <= 0 || source->channelCount == 0)
                    continue;

                auto& host = audio_source_hosts[audioSourceId];
                host = HostAudioSource{
                    .id = audioSourceId,
                    .name = source->filepath.empty() ? audioSourceId : source->filepath,
                    .persistent_id = "uapmd.audio-source." + audioSourceId
                };
                ARA::ARAAudioSourceProperties sourceProperties{
                    .structSize = ARA::kARAAudioSourcePropertiesMinSize,
                    .name = host.name.c_str(),
                    .persistentID = host.persistent_id.c_str(),
                    .sampleCount = source->frameCount,
                    .sampleRate = source->sampleRate,
                    .channelCount = static_cast<ARA::ARAChannelCount>(source->channelCount),
                    .merits64BitSamples = ARA::kARAFalse,
                    .channelArrangementDataType = ARA::kARAChannelArrangementUndefined,
                    .channelArrangement = nullptr
                };
                ARA::ARAAudioSourceRef sourceRef{};
                if (controller->createAudioSource)
                    sourceRef = controller->createAudioSource(
                        controller_instance->documentControllerRef,
                        reinterpret_cast<ARA::ARAAudioSourceHostRef>(&host),
                        &sourceProperties);
                if (!sourceRef)
                    continue;
                audio_source_refs[audioSourceId] = sourceRef;

                auto modificationId = "mod." + audioSourceId;
                auto& modificationHost = audio_modification_hosts[modificationId];
                modificationHost = HostAudioModification{
                    .id = modificationId,
                    .name = host.name,
                    .persistent_id = "uapmd.audio-modification." + audioSourceId
                };
                ARA::ARAAudioModificationProperties modificationProperties{
                    .structSize = ARA::kARAAudioModificationPropertiesMinSize,
                    .name = modificationHost.name.c_str(),
                    .persistentID = modificationHost.persistent_id.c_str()
                };
                if (controller->createAudioModification)
                    audio_modification_refs[modificationId] = controller->createAudioModification(
                        controller_instance->documentControllerRef,
                        sourceRef,
                        reinterpret_cast<ARA::ARAAudioModificationHostRef>(&modificationHost),
                        &modificationProperties);
            }

            for (const auto& trackId : view.trackIds()) {
                auto sequenceIt = region_sequence_refs.find(trackId);
                if (sequenceIt == region_sequence_refs.end())
                    continue;

                for (const auto& clipId : view.clipIds(trackId)) {
                    auto clip = view.getClip(clipId);
                    if (!clip || clip->clipType != ClipType::Audio)
                        continue;

                    ProjectObjectId audioSourceId;
                    for (const auto& candidateId : view.audioSourceIds()) {
                        auto source = view.getAudioSource(candidateId);
                        if (source && source->clipId == clipId) {
                            audioSourceId = candidateId;
                            break;
                        }
                    }
                    if (audioSourceId.empty())
                        continue;

                    auto modificationIt = audio_modification_refs.find("mod." + audioSourceId);
                    if (modificationIt == audio_modification_refs.end())
                        continue;

                    auto source = view.getAudioSource(audioSourceId);
                    const auto sourceSampleRate = source && source->sampleRate > 0 ? source->sampleRate : 48000.0;
                    auto& host = playback_region_hosts[clipId];
                    host = HostPlaybackRegion{
                        .id = clipId,
                        .name = clip->name.empty() ? clipId : clip->name
                    };
                    ARA::ARAPlaybackRegionProperties regionProperties{
                        .structSize = ARA::kARAPlaybackRegionPropertiesMinSize,
                        .transformationFlags = ARA::kARAPlaybackTransformationNoChanges,
                        .startInModificationTime = 0.0,
                        .durationInModificationTime = secondsFromSamples(clip->durationSamples, sourceSampleRate),
                        .startInPlaybackTime = secondsFromSamples(clip->position.samples, sourceSampleRate),
                        .durationInPlaybackTime = secondsFromSamples(clip->durationSamples, sourceSampleRate),
                        .musicalContextRef = musical_context_ref,
                        .regionSequenceRef = sequenceIt->second,
                        .name = host.name.c_str(),
                        .color = nullptr
                    };
                    if (controller->createPlaybackRegion)
                        playback_region_refs[clipId] = controller->createPlaybackRegion(
                            controller_instance->documentControllerRef,
                            modificationIt->second,
                            reinterpret_cast<ARA::ARAPlaybackRegionHostRef>(&host),
                            &regionProperties);
                }
            }

            endEditing();
            notifyModelUpdates();
            return true;
        }
    };

    namespace {
        ARA::ARABool readAudioSamplesFromImpl(
            AraHostDocumentController::Impl* impl,
            HostAudioReader* reader,
            ARA::ARASamplePosition samplePosition,
            ARA::ARASampleCount samplesPerChannel,
            void* const buffers[]) {
            if (!impl || !impl->document_view || !reader || !buffers)
                return ARA::kARAFalse;

            auto source = impl->document_view->getAudioSource(reader->audio_source_id);
            if (!source || source->channelCount == 0)
                return ARA::kARAFalse;

            if (!reader->use64BitSamples) {
                std::vector<float*> floatBuffers;
                floatBuffers.reserve(source->channelCount);
                for (uint32_t ch = 0; ch < source->channelCount; ch++)
                    floatBuffers.push_back(static_cast<float*>(buffers[ch]));
                return impl->document_view->readAudioSourceSamples(
                    reader->audio_source_id,
                    samplePosition,
                    samplesPerChannel,
                    floatBuffers.data(),
                    source->channelCount)
                    ? ARA::kARATrue
                    : ARA::kARAFalse;
            }

            std::vector<std::vector<float>> temp(source->channelCount);
            std::vector<float*> tempPtrs;
            tempPtrs.reserve(source->channelCount);
            for (uint32_t ch = 0; ch < source->channelCount; ch++) {
                temp[ch].resize(static_cast<size_t>(samplesPerChannel));
                tempPtrs.push_back(temp[ch].data());
            }
            if (!impl->document_view->readAudioSourceSamples(
                    reader->audio_source_id,
                    samplePosition,
                    samplesPerChannel,
                    tempPtrs.data(),
                    source->channelCount))
                return ARA::kARAFalse;

            for (uint32_t ch = 0; ch < source->channelCount; ch++) {
                auto* dst = static_cast<double*>(buffers[ch]);
                for (ARA::ARASampleCount i = 0; i < samplesPerChannel; i++)
                    dst[i] = temp[ch][static_cast<size_t>(i)];
            }
            return ARA::kARATrue;
        }
    } // namespace

    AraHostDocumentController::AraHostDocumentController(const ARA::ARAFactory& factory, std::string documentName)
        : impl_(new Impl(factory, std::move(documentName))) {
    }

    AraHostDocumentController::~AraHostDocumentController() {
        delete impl_;
    }

    bool AraHostDocumentController::valid() const {
        return impl_ && impl_->valid();
    }

    ARA::ARADocumentControllerRef AraHostDocumentController::documentControllerRef() const {
        if (!valid())
            return nullptr;
        return impl_->controller_instance->documentControllerRef;
    }

    const ARA::ARAFactory* AraHostDocumentController::factory() const {
        return impl_ ? impl_->factory : nullptr;
    }

    bool AraHostDocumentController::resyncFromProjectDocument(ProjectDocumentView& documentView) {
        if (!valid())
            return false;
        return impl_->populateModel(documentView);
    }

    void AraHostDocumentController::notifyModelUpdates() {
        if (!valid())
            return;
        impl_->notifyModelUpdates();
    }

} // namespace uapmd::ara
