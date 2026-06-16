#include "AraHostDocumentController.hpp"

#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <string>
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

        struct HostContentReader {
            ARA::ARAContentType content_type{};
            std::vector<ARA::ARAContentTempoEntry> tempo_entries{};
            std::vector<ARA::ARAContentBarSignature> bar_signatures{};
            std::vector<ARA::ARAContentTuning> tunings{};
        };

        struct HostArchive {
            const std::vector<uint8_t>* read_data{};
            std::vector<uint8_t>* write_data{};
            std::string archive_id{"uapmd-ara-document"};
        };

        struct PendingAnalysisRequest {
            ProjectObjectId audio_source_id;
            std::vector<AraContentKind> requested_kinds{};
            std::vector<ARA::ARAContentType> content_types{};
            AraAnalysisCallback callback{};
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

        ARA::ARABool isMusicalContentAvailableFromImpl(
            AraHostDocumentController::Impl* impl,
            HostRegionSequence* musicalContextHost,
            ARA::ARAContentType contentType);

        ARA::ARAContentGrade getMusicalContentGradeFromImpl(
            AraHostDocumentController::Impl* impl,
            HostRegionSequence* musicalContextHost,
            ARA::ARAContentType contentType);

        ARA::ARAContentReaderHostRef createMusicalContentReaderFromImpl(
            AraHostDocumentController::Impl* impl,
            HostRegionSequence* musicalContextHost,
            ARA::ARAContentType contentType,
            const ARA::ARAContentTimeRange* range);

        void notifyAudioSourceAnalysisProgressFromImpl(
            AraHostDocumentController::Impl* impl,
            HostAudioSource* audioSourceHost,
            ARA::ARAAnalysisProgressState state,
            float value);

        void notifyAudioSourceContentChangedFromImpl(
            AraHostDocumentController::Impl* impl,
            HostAudioSource* audioSourceHost);

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

        ARA::ARABool ARA_CALL isMusicalContextContentAvailable(
            ARA::ARAContentAccessControllerHostRef controllerHostRef,
            ARA::ARAMusicalContextHostRef musicalContextHostRef,
            ARA::ARAContentType contentType) {
            return isMusicalContentAvailableFromImpl(
                reinterpret_cast<AraHostDocumentController::Impl*>(controllerHostRef),
                reinterpret_cast<HostRegionSequence*>(musicalContextHostRef),
                contentType);
        }

        ARA::ARAContentGrade ARA_CALL getMusicalContextContentGrade(
            ARA::ARAContentAccessControllerHostRef controllerHostRef,
            ARA::ARAMusicalContextHostRef musicalContextHostRef,
            ARA::ARAContentType contentType) {
            return getMusicalContentGradeFromImpl(
                reinterpret_cast<AraHostDocumentController::Impl*>(controllerHostRef),
                reinterpret_cast<HostRegionSequence*>(musicalContextHostRef),
                contentType);
        }

        ARA::ARAContentReaderHostRef ARA_CALL createMusicalContextContentReader(
            ARA::ARAContentAccessControllerHostRef controllerHostRef,
            ARA::ARAMusicalContextHostRef musicalContextHostRef,
            ARA::ARAContentType contentType,
            const ARA::ARAContentTimeRange* range) {
            return createMusicalContentReaderFromImpl(
                reinterpret_cast<AraHostDocumentController::Impl*>(controllerHostRef),
                reinterpret_cast<HostRegionSequence*>(musicalContextHostRef),
                contentType,
                range);
        }

        ARA::ARABool ARA_CALL isAudioSourceContentAvailable(
            ARA::ARAContentAccessControllerHostRef controllerHostRef,
            ARA::ARAAudioSourceHostRef audioSourceHostRef,
            ARA::ARAContentType contentType) {
            (void) controllerHostRef;
            (void) audioSourceHostRef;
            (void) contentType;
            return ARA::kARAFalse;
        }

        ARA::ARAContentGrade ARA_CALL getAudioSourceContentGrade(
            ARA::ARAContentAccessControllerHostRef controllerHostRef,
            ARA::ARAAudioSourceHostRef audioSourceHostRef,
            ARA::ARAContentType contentType) {
            (void) controllerHostRef;
            (void) audioSourceHostRef;
            (void) contentType;
            return ARA::kARAContentGradeInitial;
        }

        ARA::ARAContentReaderHostRef ARA_CALL createAudioSourceContentReader(
            ARA::ARAContentAccessControllerHostRef controllerHostRef,
            ARA::ARAAudioSourceHostRef audioSourceHostRef,
            ARA::ARAContentType contentType,
            const ARA::ARAContentTimeRange* range) {
            (void) controllerHostRef;
            (void) audioSourceHostRef;
            (void) contentType;
            (void) range;
            return nullptr;
        }

        ARA::ARAInt32 ARA_CALL getContentReaderEventCount(
            ARA::ARAContentAccessControllerHostRef controllerHostRef,
            ARA::ARAContentReaderHostRef contentReaderHostRef) {
            (void) controllerHostRef;
            auto* reader = reinterpret_cast<HostContentReader*>(contentReaderHostRef);
            if (!reader)
                return 0;
            switch (reader->content_type) {
                case ARA::kARAContentTypeTempoEntries:
                    return static_cast<ARA::ARAInt32>(reader->tempo_entries.size());
                case ARA::kARAContentTypeBarSignatures:
                    return static_cast<ARA::ARAInt32>(reader->bar_signatures.size());
                case ARA::kARAContentTypeStaticTuning:
                    return static_cast<ARA::ARAInt32>(reader->tunings.size());
                default:
                    return 0;
            }
        }

        const void* ARA_CALL getContentReaderDataForEvent(
            ARA::ARAContentAccessControllerHostRef controllerHostRef,
            ARA::ARAContentReaderHostRef contentReaderHostRef,
            ARA::ARAInt32 eventIndex) {
            (void) controllerHostRef;
            auto* reader = reinterpret_cast<HostContentReader*>(contentReaderHostRef);
            if (!reader || eventIndex < 0)
                return nullptr;
            const auto index = static_cast<size_t>(eventIndex);
            switch (reader->content_type) {
                case ARA::kARAContentTypeTempoEntries:
                    if (index >= reader->tempo_entries.size())
                        return nullptr;
                    return &reader->tempo_entries[index];
                case ARA::kARAContentTypeBarSignatures:
                    if (index >= reader->bar_signatures.size())
                        return nullptr;
                    return &reader->bar_signatures[index];
                case ARA::kARAContentTypeStaticTuning:
                    if (index >= reader->tunings.size())
                        return nullptr;
                    return &reader->tunings[index];
                default:
                    return nullptr;
            }
        }

        void ARA_CALL destroyContentReader(
            ARA::ARAContentAccessControllerHostRef controllerHostRef,
            ARA::ARAContentReaderHostRef contentReaderHostRef) {
            (void) controllerHostRef;
            delete reinterpret_cast<HostContentReader*>(contentReaderHostRef);
        }

        ARA::ARASize ARA_CALL getArchiveSize(
            ARA::ARAArchivingControllerHostRef controllerHostRef,
            ARA::ARAArchiveReaderHostRef archiveReaderHostRef) {
            (void) controllerHostRef;
            auto* archive = reinterpret_cast<HostArchive*>(archiveReaderHostRef);
            if (!archive || !archive->read_data)
                return 0;
            return static_cast<ARA::ARASize>(archive->read_data->size());
        }

        ARA::ARABool ARA_CALL readBytesFromArchive(
            ARA::ARAArchivingControllerHostRef controllerHostRef,
            ARA::ARAArchiveReaderHostRef archiveReaderHostRef,
            ARA::ARASize position,
            ARA::ARASize length,
            ARA::ARAByte buffer[]) {
            (void) controllerHostRef;
            auto* archive = reinterpret_cast<HostArchive*>(archiveReaderHostRef);
            if (!archive || !archive->read_data || !buffer)
                return ARA::kARAFalse;
            const auto start = static_cast<size_t>(position);
            const auto size = static_cast<size_t>(length);
            if (start > archive->read_data->size() || size > archive->read_data->size() - start)
                return ARA::kARAFalse;
            if (size > 0)
                std::memcpy(buffer, archive->read_data->data() + start, size);
            return ARA::kARATrue;
        }

        ARA::ARABool ARA_CALL writeBytesToArchive(
            ARA::ARAArchivingControllerHostRef controllerHostRef,
            ARA::ARAArchiveWriterHostRef archiveWriterHostRef,
            ARA::ARASize position,
            ARA::ARASize length,
            const ARA::ARAByte buffer[]) {
            (void) controllerHostRef;
            auto* archive = reinterpret_cast<HostArchive*>(archiveWriterHostRef);
            if (!archive || !archive->write_data || (!buffer && length > 0))
                return ARA::kARAFalse;
            const auto start = static_cast<size_t>(position);
            const auto size = static_cast<size_t>(length);
            if (start > archive->write_data->max_size() || size > archive->write_data->max_size() - start)
                return ARA::kARAFalse;
            const auto requiredSize = start + size;
            if (archive->write_data->size() < requiredSize)
                archive->write_data->resize(requiredSize);
            if (size > 0)
                std::memcpy(archive->write_data->data() + start, buffer, size);
            return ARA::kARATrue;
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
            auto* archive = reinterpret_cast<HostArchive*>(archiveReaderHostRef);
            if (!archive || archive->archive_id.empty())
                return nullptr;
            return archive->archive_id.c_str();
        }

        void ARA_CALL notifyAudioSourceAnalysisProgress(
            ARA::ARAModelUpdateControllerHostRef controllerHostRef,
            ARA::ARAAudioSourceHostRef audioSourceHostRef,
            ARA::ARAAnalysisProgressState state,
            float value) {
            notifyAudioSourceAnalysisProgressFromImpl(
                reinterpret_cast<AraHostDocumentController::Impl*>(controllerHostRef),
                reinterpret_cast<HostAudioSource*>(audioSourceHostRef),
                state,
                value);
        }

        void ARA_CALL notifyAudioSourceContentChanged(
            ARA::ARAModelUpdateControllerHostRef controllerHostRef,
            ARA::ARAAudioSourceHostRef audioSourceHostRef,
            const ARA::ARAContentTimeRange* range,
            ARA::ARAContentUpdateFlags flags) {
            (void) range;
            (void) flags;
            notifyAudioSourceContentChangedFromImpl(
                reinterpret_cast<AraHostDocumentController::Impl*>(controllerHostRef),
                reinterpret_cast<HostAudioSource*>(audioSourceHostRef));
        }

        void ARA_CALL notifyAudioModificationContentChanged(
            ARA::ARAModelUpdateControllerHostRef controllerHostRef,
            ARA::ARAAudioModificationHostRef audioModificationHostRef,
            const ARA::ARAContentTimeRange* range,
            ARA::ARAContentUpdateFlags flags) {
            (void) controllerHostRef;
            (void) audioModificationHostRef;
            (void) range;
            (void) flags;
        }

        void ARA_CALL notifyPlaybackRegionContentChanged(
            ARA::ARAModelUpdateControllerHostRef controllerHostRef,
            ARA::ARAPlaybackRegionHostRef playbackRegionHostRef,
            const ARA::ARAContentTimeRange* range,
            ARA::ARAContentUpdateFlags flags) {
            (void) controllerHostRef;
            (void) playbackRegionHostRef;
            (void) range;
            (void) flags;
        }

        void ARA_CALL notifyDocumentDataChanged(
            ARA::ARAModelUpdateControllerHostRef controllerHostRef) {
            (void) controllerHostRef;
        }

        void ARA_CALL notifyRegionSequenceDataChanged(
            ARA::ARAModelUpdateControllerHostRef controllerHostRef,
            ARA::ARARegionSequenceHostRef regionSequenceHostRef) {
            (void) controllerHostRef;
            (void) regionSequenceHostRef;
        }
    } // namespace

    struct AraHostDocumentController::Impl {
        const ARA::ARAFactory* factory{};
        std::string document_name;
        ARA::ARAInterfaceConfiguration interface_configuration{};
        ARA::ARAAudioAccessControllerInterface audio_access_interface{};
        ARA::ARAContentAccessControllerInterface content_access_interface{};
        ARA::ARAArchivingControllerInterface archiving_interface{};
        ARA::ARAModelUpdateControllerInterface model_update_interface{};
        ARA::ARADocumentControllerHostInstance host_instance{};
        ARA::ARADocumentProperties document_properties{};
        const ARA::ARADocumentControllerInstance* controller_instance{};
        bool initialized_factory{};
        ProjectDocumentView* document_view{};
        TimelineFacade::MasterTrackSnapshot master_track_snapshot{};
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
        std::map<ProjectObjectId, ProjectObjectId> playback_region_track_ids{};
        std::map<ProjectObjectId, ProjectObjectId> playback_region_audio_source_ids{};
        std::map<AraRequestId, PendingAnalysisRequest> pending_analysis_requests{};
        AraRequestId next_analysis_request_id{1};

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
            content_access_interface = ARA::ARAContentAccessControllerInterface{
                .structSize = ARA::kARAContentAccessControllerInterfaceMinSize,
                .isMusicalContextContentAvailable = isMusicalContextContentAvailable,
                .getMusicalContextContentGrade = getMusicalContextContentGrade,
                .createMusicalContextContentReader = createMusicalContextContentReader,
                .isAudioSourceContentAvailable = isAudioSourceContentAvailable,
                .getAudioSourceContentGrade = getAudioSourceContentGrade,
                .createAudioSourceContentReader = createAudioSourceContentReader,
                .getContentReaderEventCount = getContentReaderEventCount,
                .getContentReaderDataForEvent = getContentReaderDataForEvent,
                .destroyContentReader = destroyContentReader
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
            model_update_interface = ARA::ARAModelUpdateControllerInterface{
                .structSize = sizeof(ARA::ARAModelUpdateControllerInterface),
                .notifyAudioSourceAnalysisProgress = notifyAudioSourceAnalysisProgress,
                .notifyAudioSourceContentChanged = notifyAudioSourceContentChanged,
                .notifyAudioModificationContentChanged = notifyAudioModificationContentChanged,
                .notifyPlaybackRegionContentChanged = notifyPlaybackRegionContentChanged,
                .notifyDocumentDataChanged = notifyDocumentDataChanged,
                .notifyRegionSequenceDataChanged = notifyRegionSequenceDataChanged
            };
            host_instance = ARA::ARADocumentControllerHostInstance{
                .structSize = ARA::kARADocumentControllerHostInstanceMinSize,
                .audioAccessControllerHostRef = reinterpret_cast<ARA::ARAAudioAccessControllerHostRef>(this),
                .audioAccessControllerInterface = &audio_access_interface,
                .archivingControllerHostRef = reinterpret_cast<ARA::ARAArchivingControllerHostRef>(this),
                .archivingControllerInterface = &archiving_interface,
                .contentAccessControllerHostRef = reinterpret_cast<ARA::ARAContentAccessControllerHostRef>(this),
                .contentAccessControllerInterface = &content_access_interface,
                .modelUpdateControllerHostRef = reinterpret_cast<ARA::ARAModelUpdateControllerHostRef>(this),
                .modelUpdateControllerInterface = &model_update_interface,
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
            playback_region_track_ids.clear();
            playback_region_audio_source_ids.clear();

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

        bool populateModel(
            ProjectDocumentView& view,
            const TimelineFacade::MasterTrackSnapshot& masterTrackSnapshot) {
            auto* controller = controllerInterface();
            if (!controller)
                return false;

            document_view = &view;
            master_track_snapshot = masterTrackSnapshot;
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
                if (controller->enableAudioSourceSamplesAccess)
                    controller->enableAudioSourceSamplesAccess(
                        controller_instance->documentControllerRef,
                        sourceRef,
                        ARA::kARATrue);

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
                    if (playback_region_refs.contains(clipId)) {
                        playback_region_track_ids[clipId] = trackId;
                        playback_region_audio_source_ids[clipId] = audioSourceId;
                    }
                }
            }

            endEditing();
            notifyModelUpdates();
            return true;
        }

        std::optional<ProjectObjectId> audioSourceIdForClip(ProjectDocumentView& view, const ProjectObjectId& clipId) {
            for (const auto& audioSourceId : view.audioSourceIds()) {
                auto source = view.getAudioSource(audioSourceId);
                if (source && source->clipId == clipId)
                    return audioSourceId;
            }
            return std::nullopt;
        }

        ARA::ARAMusicalContextProperties musicalContextProperties() const {
            return ARA::ARAMusicalContextProperties{
                .structSize = ARA::kARAMusicalContextPropertiesMinSize,
                .name = musical_context_host.name.c_str(),
                .orderIndex = 0,
                .color = nullptr
            };
        }

        ARA::ARARegionSequenceProperties regionSequenceProperties(const ProjectTrackSnapshot& track, HostRegionSequence& host) const {
            return ARA::ARARegionSequenceProperties{
                .structSize = ARA::kARARegionSequencePropertiesMinSize,
                .name = host.name.c_str(),
                .orderIndex = track.trackIndex,
                .musicalContextRef = musical_context_ref,
                .color = nullptr,
                .persistentID = host.persistent_id.c_str()
            };
        }

        ARA::ARAAudioSourceProperties audioSourceProperties(const ProjectAudioSourceSnapshot& source, HostAudioSource& host) const {
            return ARA::ARAAudioSourceProperties{
                .structSize = ARA::kARAAudioSourcePropertiesMinSize,
                .name = host.name.c_str(),
                .persistentID = host.persistent_id.c_str(),
                .sampleCount = source.frameCount,
                .sampleRate = source.sampleRate,
                .channelCount = static_cast<ARA::ARAChannelCount>(source.channelCount),
                .merits64BitSamples = ARA::kARAFalse,
                .channelArrangementDataType = ARA::kARAChannelArrangementUndefined,
                .channelArrangement = nullptr
            };
        }

        ARA::ARAAudioModificationProperties audioModificationProperties(HostAudioModification& host) const {
            return ARA::ARAAudioModificationProperties{
                .structSize = ARA::kARAAudioModificationPropertiesMinSize,
                .name = host.name.c_str(),
                .persistentID = host.persistent_id.c_str()
            };
        }

        std::optional<ARA::ARAPlaybackRegionProperties> playbackRegionProperties(
            ProjectDocumentView& view,
            const ProjectClipSnapshot& clip,
            HostPlaybackRegion& host,
            ARA::ARARegionSequenceRef regionSequenceRef) {
            auto audioSourceId = audioSourceIdForClip(view, clip.clipId);
            if (!audioSourceId)
                return std::nullopt;

            auto source = view.getAudioSource(*audioSourceId);
            const auto sourceSampleRate = source && source->sampleRate > 0 ? source->sampleRate : 48000.0;
            return ARA::ARAPlaybackRegionProperties{
                .structSize = ARA::kARAPlaybackRegionPropertiesMinSize,
                .transformationFlags = ARA::kARAPlaybackTransformationNoChanges,
                .startInModificationTime = 0.0,
                .durationInModificationTime = secondsFromSamples(clip.durationSamples, sourceSampleRate),
                .startInPlaybackTime = secondsFromSamples(clip.position.samples, sourceSampleRate),
                .durationInPlaybackTime = secondsFromSamples(clip.durationSamples, sourceSampleRate),
                .musicalContextRef = musical_context_ref,
                .regionSequenceRef = regionSequenceRef,
                .name = host.name.c_str(),
                .color = nullptr
            };
        }

        bool createOrUpdateRegionSequence(ProjectDocumentView& view, const ProjectObjectId& trackId) {
            auto* controller = controllerInterface();
            auto track = view.getTrack(trackId);
            if (!controller || !track || track->masterTrack)
                return false;

            auto& host = region_sequence_hosts[trackId];
            host = HostRegionSequence{
                .id = trackId,
                .name = "Track " + std::to_string(track->trackIndex),
                .persistent_id = "uapmd.track." + trackId
            };
            auto properties = regionSequenceProperties(*track, host);
            auto existing = region_sequence_refs.find(trackId);
            if (existing != region_sequence_refs.end()) {
                if (controller->updateRegionSequenceProperties)
                    controller->updateRegionSequenceProperties(
                        controller_instance->documentControllerRef,
                        existing->second,
                        &properties);
                return true;
            }

            if (!controller->createRegionSequence)
                return false;
            auto ref = controller->createRegionSequence(
                controller_instance->documentControllerRef,
                reinterpret_cast<ARA::ARARegionSequenceHostRef>(&host),
                &properties);
            if (!ref)
                return false;
            region_sequence_refs[trackId] = ref;
            return true;
        }

        void destroyPlaybackRegion(const ProjectObjectId& clipId) {
            auto* controller = controllerInterface();
            auto it = playback_region_refs.find(clipId);
            if (controller && it != playback_region_refs.end() && controller->destroyPlaybackRegion)
                controller->destroyPlaybackRegion(controller_instance->documentControllerRef, it->second);
            playback_region_refs.erase(clipId);
            playback_region_hosts.erase(clipId);
            playback_region_track_ids.erase(clipId);
            playback_region_audio_source_ids.erase(clipId);
        }

        void destroyRegionSequence(const ProjectObjectId& trackId) {
            std::vector<ProjectObjectId> clipIds;
            for (const auto& [clipId, ownerTrackId] : playback_region_track_ids)
                if (ownerTrackId == trackId)
                    clipIds.push_back(clipId);
            for (const auto& clipId : clipIds)
                destroyPlaybackRegion(clipId);

            auto* controller = controllerInterface();
            auto it = region_sequence_refs.find(trackId);
            if (controller && it != region_sequence_refs.end() && controller->destroyRegionSequence)
                controller->destroyRegionSequence(controller_instance->documentControllerRef, it->second);
            region_sequence_refs.erase(trackId);
            region_sequence_hosts.erase(trackId);
        }

        bool createOrUpdateAudioSource(ProjectDocumentView& view, const ProjectObjectId& audioSourceId) {
            auto* controller = controllerInterface();
            auto source = view.getAudioSource(audioSourceId);
            if (!controller || !source || source->frameCount <= 0 || source->channelCount == 0)
                return false;

            auto& host = audio_source_hosts[audioSourceId];
            host = HostAudioSource{
                .id = audioSourceId,
                .name = source->filepath.empty() ? audioSourceId : source->filepath,
                .persistent_id = "uapmd.audio-source." + audioSourceId
            };
            auto properties = audioSourceProperties(*source, host);
            auto existing = audio_source_refs.find(audioSourceId);
            if (existing != audio_source_refs.end()) {
                if (controller->updateAudioSourceProperties)
                    controller->updateAudioSourceProperties(
                        controller_instance->documentControllerRef,
                        existing->second,
                        &properties);
                if (controller->updateAudioSourceContent)
                    controller->updateAudioSourceContent(
                        controller_instance->documentControllerRef,
                        existing->second,
                        nullptr,
                        ARA::kARAContentUpdateEverythingChanged);
                return true;
            }

            if (!controller->createAudioSource)
                return false;
            auto sourceRef = controller->createAudioSource(
                controller_instance->documentControllerRef,
                reinterpret_cast<ARA::ARAAudioSourceHostRef>(&host),
                &properties);
            if (!sourceRef)
                return false;
            audio_source_refs[audioSourceId] = sourceRef;
            if (controller->enableAudioSourceSamplesAccess)
                controller->enableAudioSourceSamplesAccess(
                    controller_instance->documentControllerRef,
                    sourceRef,
                    ARA::kARATrue);

            const auto modificationId = "mod." + audioSourceId;
            auto& modificationHost = audio_modification_hosts[modificationId];
            modificationHost = HostAudioModification{
                .id = modificationId,
                .name = host.name,
                .persistent_id = "uapmd.audio-modification." + audioSourceId
            };
            auto modificationProps = audioModificationProperties(modificationHost);
            if (controller->createAudioModification)
                audio_modification_refs[modificationId] = controller->createAudioModification(
                    controller_instance->documentControllerRef,
                    sourceRef,
                    reinterpret_cast<ARA::ARAAudioModificationHostRef>(&modificationHost),
                    &modificationProps);
            return audio_modification_refs.contains(modificationId);
        }

        void destroyAudioSource(const ProjectObjectId& audioSourceId) {
            std::vector<ProjectObjectId> clipIds;
            for (const auto& [clipId, ownerAudioSourceId] : playback_region_audio_source_ids)
                if (ownerAudioSourceId == audioSourceId)
                    clipIds.push_back(clipId);
            for (const auto& clipId : clipIds)
                destroyPlaybackRegion(clipId);

            auto* controller = controllerInterface();
            const auto modificationId = "mod." + audioSourceId;
            auto modificationIt = audio_modification_refs.find(modificationId);
            if (controller && modificationIt != audio_modification_refs.end() && controller->destroyAudioModification)
                controller->destroyAudioModification(controller_instance->documentControllerRef, modificationIt->second);
            audio_modification_refs.erase(modificationId);
            audio_modification_hosts.erase(modificationId);

            auto sourceIt = audio_source_refs.find(audioSourceId);
            if (controller && sourceIt != audio_source_refs.end() && controller->destroyAudioSource)
                controller->destroyAudioSource(controller_instance->documentControllerRef, sourceIt->second);
            audio_source_refs.erase(audioSourceId);
            audio_source_hosts.erase(audioSourceId);
        }

        bool createOrUpdatePlaybackRegion(ProjectDocumentView& view, const ProjectObjectId& clipId) {
            auto* controller = controllerInterface();
            auto clip = view.getClip(clipId);
            if (!controller || !clip || clip->clipType != ClipType::Audio)
                return false;

            if (!region_sequence_refs.contains(clip->trackId) &&
                !createOrUpdateRegionSequence(view, clip->trackId))
                return false;

            auto audioSourceId = audioSourceIdForClip(view, clipId);
            if (!audioSourceId)
                return false;
            if (!audio_modification_refs.contains("mod." + *audioSourceId) &&
                !createOrUpdateAudioSource(view, *audioSourceId))
                return false;

            auto sequenceIt = region_sequence_refs.find(clip->trackId);
            auto modificationIt = audio_modification_refs.find("mod." + *audioSourceId);
            if (sequenceIt == region_sequence_refs.end() || modificationIt == audio_modification_refs.end())
                return false;

            auto& host = playback_region_hosts[clipId];
            host = HostPlaybackRegion{
                .id = clipId,
                .name = clip->name.empty() ? clipId : clip->name
            };
            auto properties = playbackRegionProperties(view, *clip, host, sequenceIt->second);
            if (!properties)
                return false;

            auto existing = playback_region_refs.find(clipId);
            if (existing != playback_region_refs.end()) {
                if (controller->updatePlaybackRegionProperties)
                    controller->updatePlaybackRegionProperties(
                        controller_instance->documentControllerRef,
                        existing->second,
                        &*properties);
            } else {
                if (!controller->createPlaybackRegion)
                    return false;
                auto ref = controller->createPlaybackRegion(
                    controller_instance->documentControllerRef,
                    modificationIt->second,
                    reinterpret_cast<ARA::ARAPlaybackRegionHostRef>(&host),
                    &*properties);
                if (!ref)
                    return false;
                playback_region_refs[clipId] = ref;
            }

            playback_region_track_ids[clipId] = clip->trackId;
            playback_region_audio_source_ids[clipId] = *audioSourceId;
            return true;
        }

        bool updateMusicalContextContent(const TimelineFacade::MasterTrackSnapshot& masterTrackSnapshot) {
            auto* controller = controllerInterface();
            if (!controller || !musical_context_ref)
                return false;
            master_track_snapshot = masterTrackSnapshot;
            if (controller->updateMusicalContextProperties) {
                auto properties = musicalContextProperties();
                controller->updateMusicalContextProperties(
                    controller_instance->documentControllerRef,
                    musical_context_ref,
                    &properties);
            }
            if (controller->updateMusicalContextContent)
                controller->updateMusicalContextContent(
                    controller_instance->documentControllerRef,
                    musical_context_ref,
                    nullptr,
                    ARA::kARAContentUpdateEverythingChanged);
            return true;
        }

        bool applyProjectDocumentEvent(
            ProjectDocumentView& view,
            const TimelineFacade::MasterTrackSnapshot& masterTrackSnapshot,
            const ProjectDocumentEvent& event) {
            if (!valid())
                return false;
            if (event.fullResyncRecommended())
                return populateModel(view, masterTrackSnapshot);

            document_view = &view;
            bool handled = false;
            beginEditing();
            switch (event.kind()) {
                case ProjectDocumentEventKind::MasterTrackChanged:
                    handled = updateMusicalContextContent(masterTrackSnapshot);
                    break;
                case ProjectDocumentEventKind::TrackAdded:
                case ProjectDocumentEventKind::TrackChanged:
                    if (event.trackId())
                        handled = createOrUpdateRegionSequence(view, *event.trackId());
                    break;
                case ProjectDocumentEventKind::TrackRemoved:
                    if (event.trackId()) {
                        destroyRegionSequence(*event.trackId());
                        handled = true;
                    }
                    break;
                case ProjectDocumentEventKind::AudioSourceAdded:
                case ProjectDocumentEventKind::AudioSourceChanged:
                    if (event.audioSourceId())
                        handled = createOrUpdateAudioSource(view, *event.audioSourceId());
                    break;
                case ProjectDocumentEventKind::AudioSourceRemoved:
                    if (event.audioSourceId()) {
                        destroyAudioSource(*event.audioSourceId());
                        handled = true;
                    }
                    break;
                case ProjectDocumentEventKind::ClipAdded:
                case ProjectDocumentEventKind::ClipChanged:
                    if (event.clipId())
                        handled = createOrUpdatePlaybackRegion(view, *event.clipId());
                    break;
                case ProjectDocumentEventKind::ClipRemoved:
                    if (event.clipId()) {
                        destroyPlaybackRegion(*event.clipId());
                        handled = true;
                    }
                    break;
                case ProjectDocumentEventKind::ProjectLoaded:
                case ProjectDocumentEventKind::ProjectClosing:
                case ProjectDocumentEventKind::ProjectSaved:
                case ProjectDocumentEventKind::PluginGraphChanged:
                    break;
            }
            endEditing();

            if (!handled)
                return populateModel(view, masterTrackSnapshot);
            notifyModelUpdates();
            return true;
        }

        std::optional<ARA::ARAContentType> araContentTypeForKind(AraContentKind kind) const {
            switch (kind) {
                case AraContentKind::Notes:
                    return ARA::kARAContentTypeNotes;
                case AraContentKind::TempoMap:
                    return ARA::kARAContentTypeTempoEntries;
                case AraContentKind::TimeSignatures:
                    return ARA::kARAContentTypeBarSignatures;
                case AraContentKind::Keys:
                    return ARA::kARAContentTypeKeySignatures;
                case AraContentKind::Chords:
                    return ARA::kARAContentTypeSheetChords;
                case AraContentKind::Lyrics:
                    return ARA::kARAContentTypeLyricEntries;
                default:
                    return std::nullopt;
            }
        }

        bool factoryCanAnalyze(ARA::ARAContentType contentType) const {
            if (!factory)
                return false;
            for (ARA::ARASize i = 0; i < factory->analyzeableContentTypesCount; i++)
                if (factory->analyzeableContentTypes && factory->analyzeableContentTypes[i] == contentType)
                    return true;
            return false;
        }

        bool analysisIncomplete(ARA::ARAAudioSourceRef audioSourceRef, ARA::ARAContentType contentType) const {
            auto* controller = controllerInterface();
            if (!controller || !audioSourceRef || !controller->isAudioSourceContentAnalysisIncomplete)
                return false;
            return controller->isAudioSourceContentAnalysisIncomplete(
                controller_instance->documentControllerRef,
                audioSourceRef,
                contentType) != ARA::kARAFalse;
        }

        void completeFinishedAnalysisRequests(const ProjectObjectId& audioSourceId, bool forceComplete) {
            auto audioSourceIt = audio_source_refs.find(audioSourceId);
            if (audioSourceIt == audio_source_refs.end())
                return;

            struct Completion {
                AraAnalysisCallback callback{};
                AraAnalysisResult result{};
            };
            std::vector<Completion> completions;
            std::vector<AraRequestId> completedIds;

            for (const auto& [requestId, request] : pending_analysis_requests) {
                if (request.audio_source_id != audioSourceId)
                    continue;

                bool complete = forceComplete;
                if (!complete) {
                    complete = true;
                    for (auto contentType : request.content_types) {
                        if (analysisIncomplete(audioSourceIt->second, contentType)) {
                            complete = false;
                            break;
                        }
                    }
                }

                if (!complete)
                    continue;

                completedIds.push_back(requestId);
                completions.push_back(Completion{
                    .callback = request.callback,
                    .result = AraAnalysisResult{
                        .requestId = requestId,
                        .objectId = audioSourceId,
                        .completedKinds = request.requested_kinds,
                        .status = AraStatus::Ok,
                        .error = {}
                    }
                });
            }

            for (auto requestId : completedIds)
                pending_analysis_requests.erase(requestId);
            for (auto& completion : completions)
                if (completion.callback)
                    completion.callback(completion.result);
        }

        AraRequestId requestAnalysis(AraAnalysisRequest request, AraAnalysisCallback callback) {
            auto* controller = controllerInterface();
            if (!controller || !controller->requestAudioSourceContentAnalysis)
                return 0;

            auto audioSourceIt = audio_source_refs.find(request.objectId);
            if (audioSourceIt == audio_source_refs.end())
                return 0;

            std::vector<AraContentKind> requestedKinds;
            std::vector<ARA::ARAContentType> contentTypes;
            for (auto kind : request.contentKinds) {
                auto contentType = araContentTypeForKind(kind);
                if (!contentType || !factoryCanAnalyze(*contentType))
                    continue;
                requestedKinds.push_back(kind);
                contentTypes.push_back(*contentType);
            }

            if (contentTypes.empty())
                return 0;

            const auto requestId = next_analysis_request_id++;
            pending_analysis_requests[requestId] = PendingAnalysisRequest{
                .audio_source_id = request.objectId,
                .requested_kinds = std::move(requestedKinds),
                .content_types = contentTypes,
                .callback = std::move(callback)
            };

            controller->requestAudioSourceContentAnalysis(
                controller_instance->documentControllerRef,
                audioSourceIt->second,
                static_cast<ARA::ARASize>(contentTypes.size()),
                contentTypes.data());
            completeFinishedAnalysisRequests(request.objectId, false);
            return requestId;
        }

        void cancelAnalysis(AraRequestId requestId) {
            pending_analysis_requests.erase(requestId);
        }

        bool saveArchiveState(std::vector<uint8_t>& archive) {
            auto* controller = controllerInterface();
            if (!controller)
                return false;

            archive.clear();
            HostArchive hostArchive{
                .read_data = nullptr,
                .write_data = &archive,
                .archive_id = "uapmd-ara-document"
            };

            if (controller->storeObjectsToArchive) {
                std::vector<ARA::ARAAudioSourceRef> audioSourceRefs;
                std::vector<ARA::ARAAudioModificationRef> audioModificationRefs;
                std::vector<ARA::ARARegionSequenceRef> regionSequenceRefs;
                audioSourceRefs.reserve(audio_source_refs.size());
                audioModificationRefs.reserve(audio_modification_refs.size());
                regionSequenceRefs.reserve(region_sequence_refs.size());

                for (const auto& [id, ref] : audio_source_refs) {
                    (void) id;
                    if (ref)
                        audioSourceRefs.push_back(ref);
                }
                for (const auto& [id, ref] : audio_modification_refs) {
                    (void) id;
                    if (ref)
                        audioModificationRefs.push_back(ref);
                }
                for (const auto& [id, ref] : region_sequence_refs) {
                    (void) id;
                    if (ref)
                        regionSequenceRefs.push_back(ref);
                }

                ARA::ARAStoreObjectsFilter filter{
                    .structSize = sizeof(ARA::ARAStoreObjectsFilter),
                    .documentData = ARA::kARATrue,
                    .audioSourceRefsCount = static_cast<ARA::ARASize>(audioSourceRefs.size()),
                    .audioSourceRefs = audioSourceRefs.empty() ? nullptr : audioSourceRefs.data(),
                    .audioModificationRefsCount = static_cast<ARA::ARASize>(audioModificationRefs.size()),
                    .audioModificationRefs = audioModificationRefs.empty() ? nullptr : audioModificationRefs.data(),
                    .regionSequenceRefsCount = static_cast<ARA::ARASize>(regionSequenceRefs.size()),
                    .regionSequenceRefs = regionSequenceRefs.empty() ? nullptr : regionSequenceRefs.data()
                };
                return controller->storeObjectsToArchive(
                    controller_instance->documentControllerRef,
                    reinterpret_cast<ARA::ARAArchiveWriterHostRef>(&hostArchive),
                    &filter) != ARA::kARAFalse;
            }

            if (controller->storeDocumentToArchive)
                return controller->storeDocumentToArchive(
                    controller_instance->documentControllerRef,
                    reinterpret_cast<ARA::ARAArchiveWriterHostRef>(&hostArchive)) != ARA::kARAFalse;

            return false;
        }

        bool loadArchiveState(const std::vector<uint8_t>& archive) {
            auto* controller = controllerInterface();
            if (!controller || archive.empty())
                return false;

            HostArchive hostArchive{
                .read_data = &archive,
                .write_data = nullptr,
                .archive_id = "uapmd-ara-document"
            };

            bool ok = false;
            if (controller->restoreObjectsFromArchive) {
                beginEditing();
                ok = controller->restoreObjectsFromArchive(
                    controller_instance->documentControllerRef,
                    reinterpret_cast<ARA::ARAArchiveReaderHostRef>(&hostArchive),
                    nullptr) != ARA::kARAFalse;
                endEditing();
            } else if (controller->beginRestoringDocumentFromArchive && controller->endRestoringDocumentFromArchive) {
                beginEditing();
                ok = controller->beginRestoringDocumentFromArchive(
                    controller_instance->documentControllerRef,
                    reinterpret_cast<ARA::ARAArchiveReaderHostRef>(&hostArchive)) != ARA::kARAFalse;
                ok = controller->endRestoringDocumentFromArchive(
                    controller_instance->documentControllerRef,
                    reinterpret_cast<ARA::ARAArchiveReaderHostRef>(&hostArchive)) != ARA::kARAFalse && ok;
                endEditing();
            }

            if (ok)
                notifyModelUpdates();
            return ok;
        }
    };

    namespace {
        bool isSupportedMusicalContentType(ARA::ARAContentType contentType) {
            return contentType == ARA::kARAContentTypeTempoEntries ||
                contentType == ARA::kARAContentTypeBarSignatures ||
                contentType == ARA::kARAContentTypeStaticTuning;
        }

        using TempoPoint = TimelineFacade::MasterTrackSnapshot::TempoPoint;
        using TimeSignaturePoint = TimelineFacade::MasterTrackSnapshot::TimeSignaturePoint;

        std::vector<TempoPoint> normalizedTempoPoints(const TimelineFacade::MasterTrackSnapshot& timeline) {
            std::vector<TempoPoint> tempoPoints;
            tempoPoints.reserve(timeline.tempoPoints.size() + 1);
            for (const auto& point : timeline.tempoPoints)
                if (point.bpm > 0.0)
                    tempoPoints.push_back(point);
            std::stable_sort(tempoPoints.begin(), tempoPoints.end(), [](const auto& a, const auto& b) {
                return a.timeSeconds < b.timeSeconds;
            });

            if (tempoPoints.empty()) {
                tempoPoints.push_back(TempoPoint{
                    .timeSeconds = 0.0,
                    .tickPosition = 0,
                    .bpm = 120.0,
                });
                return tempoPoints;
            }

            if (tempoPoints.front().timeSeconds > 0.0)
                tempoPoints.insert(
                    tempoPoints.begin(),
                    TempoPoint{
                        .timeSeconds = 0.0,
                        .tickPosition = 0,
                        .bpm = tempoPoints.front().bpm,
                    });
            else
                tempoPoints.front().timeSeconds = 0.0;

            tempoPoints.erase(
                std::unique(
                    tempoPoints.begin(),
                    tempoPoints.end(),
                    [](const auto& a, const auto& b) {
                        return a.timeSeconds == b.timeSeconds;
                    }),
                tempoPoints.end());
            return tempoPoints;
        }

        std::vector<ARA::ARAContentTempoEntry> makeTempoEntries(const TimelineFacade::MasterTrackSnapshot& timeline) {
            auto tempoPoints = normalizedTempoPoints(timeline);
            std::vector<ARA::ARAContentTempoEntry> entries;
            entries.reserve(tempoPoints.size() + 1);

            double quarterPosition = 0.0;
            double previousTime = tempoPoints.front().timeSeconds;
            double previousBpm = tempoPoints.front().bpm > 0.0 ? tempoPoints.front().bpm : 120.0;

            entries.push_back(ARA::ARAContentTempoEntry{
                .timePosition = previousTime,
                .quarterPosition = quarterPosition
            });

            for (size_t i = 1; i < tempoPoints.size(); i++) {
                const auto& point = tempoPoints[i];
                const auto deltaSeconds = std::max(0.0, point.timeSeconds - previousTime);
                quarterPosition += deltaSeconds * previousBpm / 60.0;
                entries.push_back(ARA::ARAContentTempoEntry{
                    .timePosition = point.timeSeconds,
                    .quarterPosition = quarterPosition
                });
                previousTime = point.timeSeconds;
                previousBpm = point.bpm > 0.0 ? point.bpm : previousBpm;
            }

            const auto finalTime = std::max(timeline.maxTimeSeconds, previousTime + (240.0 / previousBpm));
            const auto finalQuarter = quarterPosition + std::max(0.0, finalTime - previousTime) * previousBpm / 60.0;
            if (entries.size() < 2 || finalTime > entries.back().timePosition)
                entries.push_back(ARA::ARAContentTempoEntry{
                    .timePosition = finalTime,
                    .quarterPosition = finalQuarter
                });

            return entries;
        }

        double quarterPositionAtTime(
            const std::vector<ARA::ARAContentTempoEntry>& entries,
            double timeSeconds) {
            if (entries.empty())
                return timeSeconds * 2.0;
            if (entries.size() == 1)
                return entries.front().quarterPosition + (timeSeconds - entries.front().timePosition) * 2.0;

            for (size_t i = 1; i < entries.size(); i++) {
                if (timeSeconds <= entries[i].timePosition) {
                    const auto& left = entries[i - 1];
                    const auto& right = entries[i];
                    const auto duration = right.timePosition - left.timePosition;
                    if (duration <= 0.0)
                        return left.quarterPosition;
                    const auto ratio = (timeSeconds - left.timePosition) / duration;
                    return left.quarterPosition + (right.quarterPosition - left.quarterPosition) * ratio;
                }
            }

            const auto& left = entries[entries.size() - 2];
            const auto& right = entries[entries.size() - 1];
            const auto duration = right.timePosition - left.timePosition;
            const auto quarters = right.quarterPosition - left.quarterPosition;
            const auto quartersPerSecond = duration > 0.0 ? quarters / duration : 2.0;
            return right.quarterPosition + (timeSeconds - right.timePosition) * quartersPerSecond;
        }

        std::vector<ARA::ARAContentBarSignature> makeBarSignatures(
            const TimelineFacade::MasterTrackSnapshot& timeline,
            const std::vector<ARA::ARAContentTempoEntry>& tempoEntries) {
            std::vector<TimeSignaturePoint> signaturePoints = timeline.timeSignaturePoints;
            std::stable_sort(signaturePoints.begin(), signaturePoints.end(), [](const auto& a, const auto& b) {
                return a.timeSeconds < b.timeSeconds;
            });

            if (signaturePoints.empty())
                signaturePoints.push_back(TimeSignaturePoint{
                    .timeSeconds = 0.0,
                    .tickPosition = 0,
                    .signature = MidiTimeSignatureChange{}
                });
            if (signaturePoints.front().timeSeconds > 0.0)
                signaturePoints.insert(
                    signaturePoints.begin(),
                    TimeSignaturePoint{
                        .timeSeconds = 0.0,
                        .tickPosition = 0,
                        .signature = signaturePoints.front().signature
                    });
            else
                signaturePoints.front().timeSeconds = 0.0;

            std::vector<ARA::ARAContentBarSignature> result;
            result.reserve(signaturePoints.size());
            for (const auto& signature : signaturePoints) {
                const auto numerator = signature.signature.numerator > 0 ? signature.signature.numerator : 4;
                const auto denominator = signature.signature.denominator > 0 ? signature.signature.denominator : 4;
                result.push_back(ARA::ARAContentBarSignature{
                    .numerator = static_cast<ARA::ARAInt32>(numerator),
                    .denominator = static_cast<ARA::ARAInt32>(denominator),
                    .position = quarterPositionAtTime(tempoEntries, signature.timeSeconds)
                });
            }
            return result;
        }

        ARA::ARABool isMusicalContentAvailableFromImpl(
            AraHostDocumentController::Impl* impl,
            HostRegionSequence* musicalContextHost,
            ARA::ARAContentType contentType) {
            if (!impl || !impl->document_view || !musicalContextHost || musicalContextHost != &impl->musical_context_host)
                return ARA::kARAFalse;
            return isSupportedMusicalContentType(contentType) ? ARA::kARATrue : ARA::kARAFalse;
        }

        ARA::ARAContentGrade getMusicalContentGradeFromImpl(
            AraHostDocumentController::Impl* impl,
            HostRegionSequence* musicalContextHost,
            ARA::ARAContentType contentType) {
            if (!isMusicalContentAvailableFromImpl(impl, musicalContextHost, contentType))
                return ARA::kARAContentGradeInitial;
            if (contentType == ARA::kARAContentTypeStaticTuning)
                return ARA::kARAContentGradeInitial;
            if (contentType == ARA::kARAContentTypeTempoEntries && impl->master_track_snapshot.tempoPoints.empty())
                return ARA::kARAContentGradeInitial;
            if (contentType == ARA::kARAContentTypeBarSignatures && impl->master_track_snapshot.timeSignaturePoints.empty())
                return ARA::kARAContentGradeInitial;
            return ARA::kARAContentGradeAdjusted;
        }

        ARA::ARAContentReaderHostRef createMusicalContentReaderFromImpl(
            AraHostDocumentController::Impl* impl,
            HostRegionSequence* musicalContextHost,
            ARA::ARAContentType contentType,
            const ARA::ARAContentTimeRange* range) {
            (void) range;
            if (!isMusicalContentAvailableFromImpl(impl, musicalContextHost, contentType))
                return nullptr;

            auto reader = std::make_unique<HostContentReader>();
            reader->content_type = contentType;
            switch (contentType) {
                case ARA::kARAContentTypeTempoEntries:
                    reader->tempo_entries = makeTempoEntries(impl->master_track_snapshot);
                    break;
                case ARA::kARAContentTypeBarSignatures: {
                    auto tempoEntries = makeTempoEntries(impl->master_track_snapshot);
                    reader->bar_signatures = makeBarSignatures(impl->master_track_snapshot, tempoEntries);
                    break;
                }
                case ARA::kARAContentTypeStaticTuning: {
                    ARA::ARAContentTuning tuning{
                        .concertPitchFrequency = ARA::kARADefaultConcertPitchFrequency,
                        .root = 0,
                        .tunings = {},
                        .name = "Equal temperament"
                    };
                    reader->tunings.push_back(tuning);
                    break;
                }
                default:
                    return nullptr;
            }
            return reinterpret_cast<ARA::ARAContentReaderHostRef>(reader.release());
        }

        void notifyAudioSourceAnalysisProgressFromImpl(
            AraHostDocumentController::Impl* impl,
            HostAudioSource* audioSourceHost,
            ARA::ARAAnalysisProgressState state,
            float value) {
            (void) value;
            if (!impl || !audioSourceHost)
                return;
            if (state == ARA::kARAAnalysisProgressCompleted)
                impl->completeFinishedAnalysisRequests(audioSourceHost->id, false);
        }

        void notifyAudioSourceContentChangedFromImpl(
            AraHostDocumentController::Impl* impl,
            HostAudioSource* audioSourceHost) {
            if (!impl || !audioSourceHost)
                return;
            impl->completeFinishedAnalysisRequests(audioSourceHost->id, false);
        }

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

    bool AraHostDocumentController::resyncFromProjectDocument(
        ProjectDocumentView& documentView,
        const TimelineFacade::MasterTrackSnapshot& masterTrackSnapshot) {
        if (!valid())
            return false;
        return impl_->populateModel(documentView, masterTrackSnapshot);
    }

    bool AraHostDocumentController::applyProjectDocumentEvent(
        ProjectDocumentView& documentView,
        const TimelineFacade::MasterTrackSnapshot& masterTrackSnapshot,
        const ProjectDocumentEvent& event) {
        if (!valid())
            return false;
        return impl_->applyProjectDocumentEvent(documentView, masterTrackSnapshot, event);
    }

    AraRequestId AraHostDocumentController::requestAnalysis(AraAnalysisRequest request, AraAnalysisCallback callback) {
        if (!valid())
            return 0;
        return impl_->requestAnalysis(std::move(request), std::move(callback));
    }

    void AraHostDocumentController::cancelAnalysis(AraRequestId requestId) {
        if (!valid())
            return;
        impl_->cancelAnalysis(requestId);
    }

    bool AraHostDocumentController::saveArchiveState(std::vector<uint8_t>& archive) {
        return valid() && impl_->saveArchiveState(archive);
    }

    bool AraHostDocumentController::loadArchiveState(const std::vector<uint8_t>& archive) {
        return valid() && impl_->loadArchiveState(archive);
    }

    void AraHostDocumentController::notifyModelUpdates() {
        if (!valid())
            return;
        impl_->notifyModelUpdates();
    }

} // namespace uapmd::ara
