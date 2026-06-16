# uapmd-ara

uapmd-ara is an AI-slop ARA implementation for uapmd.

Any human creative parts (including the regarded-as-human-creative parts based on the [text instructions](https://gist.github.com/atsushieno/637a74f13c2a091776f187ec2181e3d1)) are released under the Apache V2 license.

----

`uapmd-ara` adds ARA host support to uapmd/remidy based applications.

The library is an optional integration module. It keeps ARA-specific document controller, plug-in binding, analysis, and archive handling outside the core `uapmd` repository while using uapmd's project document, timeline event, audio source, and project serialization extension points.

## Requirements

- CMake 3.28 or newer
- C++23
- A sibling checkout of `uapmd`
- `external/ARA_SDK` inside this repository
- The same plug-in SDK dependencies used by `uapmd`/`remidy` for the formats you enable, currently VST3, CLAP, and Audio Unit where available

Expected directory layout:

```text
sources/
  uapmd/
  uapmd-ara/
    external/
      ARA_SDK/
```

## Building With uapmd

`uapmd-ara` is designed to be added from the uapmd build as a sibling project. The CMake target is:

```cmake
uapmd::ara
```

The project defaults `UAPMD_REPO_ROOT` to `../uapmd` relative to this repository. If your layout is different, set it before adding the subdirectory:

```cmake
set(UAPMD_REPO_ROOT "/path/to/uapmd")
add_subdirectory("/path/to/uapmd-ara" uapmd-ara)
target_link_libraries(your_host PRIVATE uapmd::ara)
```

The public header is:

```cpp
#include <uapmd-ara/uapmd-ara.hpp>
```

## Basic Host Integration

Create one `AraSupport` object for a `SequencerEngine`:

```cpp
auto araSupport = uapmd::ara::createAraSupport(engine);
```

Attach hosted plug-in instances using the same plug-in instance id used by the engine/host:

```cpp
auto status = araSupport->attachPlugin(pluginInstanceId, pluginInstance);
if (status == uapmd::ara::AraStatus::Ok) {
    // Native ARA document binding is active.
}
```

Detach when the plug-in instance is removed:

```cpp
araSupport->detachPlugin(pluginInstanceId);
```

`AraSupport` listens to uapmd project document events and updates the native ARA document model when tracks, clips, audio sources, or master-track musical content change.

## Plug-in Format Support

The module contains native ARA binding probes for:

- VST3
- CLAP
- Audio Unit

The native format binding creates an ARA document controller from the plug-in ARA factory and binds the plug-in extension instance to it. Unsupported plug-ins return `AraStatus::UnsupportedPlugin`.

The public API also supports remidy-level ARA extensions through `AraPluginExtension` and `AraPluginDocument`. Implement this when a plug-in backend wants to provide ARA behavior without going through one of the native format binding helpers.

## Project Data and Serialization

The module registers a uapmd project serialization extension with id:

```text
dev.atsushieno.uapmd.ara.state.v1
```

ARA plug-in state is saved as opaque ARA archive data under the project's extension-data directory. The core uapmd project model does not need to understand the archive payload.

On load, archives are restored into matching native ARA documents. If extension data is loaded before an ARA plug-in instance is attached, the archive is kept pending and restored when `attachPlugin()` is called for that instance id.

## Analysis Requests

Use `AraSupport::requestAnalysis()` to ask an attached ARA plug-in to analyze an audio source:

```cpp
uapmd::ara::AraAnalysisRequest request;
request.objectId = audioSourceId;
request.contentKinds = {
    uapmd::ara::AraContentKind::Notes,
    uapmd::ara::AraContentKind::TempoMap
};

auto requestId = araSupport->requestAnalysis(
    pluginInstanceId,
    std::move(request),
    [](const uapmd::ara::AraAnalysisResult& result) {
        // result.completedKinds contains the requested kinds that completed.
    });
```

For native ARA plug-ins, the module maps supported `AraContentKind` values to ARA content types and calls `requestAudioSourceContentAnalysis()`. Completion is reported when ARA model update notifications indicate that the requested content analysis is no longer incomplete.

`cancelAnalysis()` removes the pending host callback:

```cpp
araSupport->cancelAnalysis(pluginInstanceId, requestId);
```

ARA does not provide a per-request cancel function for analysis that has already been started inside the plug-in.

## Exposed Musical Context

The native ARA host document exposes master-track musical context from `TimelineFacade::MasterTrackSnapshot`:

- Tempo entries
- Bar signatures
- Static tuning

Audio source sample access is provided through `ProjectDocumentView::readAudioSourceSamples()`.

Clip markers, audio warps, chords, keys, and lyrics are not yet mapped as host-provided ARA content. Some of those may require additional uapmd timeline/project facade APIs or plug-in-specific behavior.

## Current Limitations

- The implementation is still early and needs validation against real ARA plug-ins across VST3, CLAP, and Audio Unit.
- ARA archives are keyed by uapmd plug-in instance id. Hosts must keep those ids stable across project save/load for reliable restoration.
- Analysis completion depends on plug-ins sending ARA model update notifications correctly.
- No public UI helper is provided. This module only provides host/document integration.
- AAP ARA integration is not implemented here.

## API Stability

The public entry point is `include/uapmd-ara/uapmd-ara.hpp`. The `src/` headers and classes are implementation details and may change without compatibility guarantees.

