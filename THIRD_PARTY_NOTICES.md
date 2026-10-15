# 제3자 구성요소 고지

각 구성요소에는 해당 라이선스가 적용됩니다. 원본 저작권·허가 문구는 아래 파일에 보존되어 있습니다. 저장소 루트의 라이선스가 모든 의존성이나 게임 자료의 라이선스를 대체하지 않습니다.

## 포함된 소스

| 구성요소 | 출처·버전 | 라이선스·고지 |
| --- | --- | --- |
| SpaceCadetPinball 코어 | [k4zmu2a/SpaceCadetPinball](https://github.com/k4zmu2a/SpaceCadetPinball), 로컬 변경 포함 | MIT, Copyright © 2020–2021 Andrey Muzychenko. [원문](engine/LICENSE) |
| Dear ImGui와 SDL 백엔드 | [ocornut/imgui](https://github.com/ocornut/imgui), 헤더 표기 `1.89.2 WIP` | MIT, Copyright © 2014–2023 Omar Cornut. [전체 고지](licenses/Dear-ImGui-LICENSE.txt). 전체 파일의 릴리스 태그 일치를 주장하지 않습니다. |
| ImGuiSDL 렌더러 | [Tyyppi77/imgui_sdl](https://github.com/Tyyppi77/imgui_sdl), SpaceCadetPinball에 포함된 사본 | MIT. [전체 고지](licenses/ImGuiSDL-LICENSE.txt). 보존한 라이선스 원문은 `7d01f15bc0035e1a8e170260ca3c0a3e0f95a257`의 문구입니다. |
| stb_truetype | Dear ImGui에 포함된 수정 사본, 헤더 표기 1.26 | MIT 또는 Public Domain 이중 고지. [전체 고지](licenses/stb-truetype-LICENSE.txt), [소스](engine/SpaceCadetPinball/imstb_truetype.h) |
| stb_rect_pack · stb_textedit | Dear ImGui에 포함된 수정 사본 | 각 소스 말미의 MIT·Public Domain 이중 고지 보존. [rect_pack](engine/SpaceCadetPinball/imstb_rectpack.h), [textedit](engine/SpaceCadetPinball/imstb_textedit.h) |
| TinySoundFont | [schellingb/TinySoundFont](https://github.com/schellingb/TinySoundFont), 커밋 `853a0a171759f1ddba0de1442133a75912bbeffa` | MIT. [라이선스](engine/SpaceCadetPinball/third_party/TinySoundFont/LICENSE) 및 [tsf.h의 고지](engine/SpaceCadetPinball/third_party/TinySoundFont/tsf.h)를 함께 보존합니다. SFZero 기반 부분의 Steve Folta 고지도 포함됩니다. |
| TinyMidiLoader | 같은 TinySoundFont 커밋의 `tml.h` | Zlib. [전체 고지](engine/SpaceCadetPinball/third_party/TinySoundFont/LICENSE-TML.txt) |
| SDL Android Java 바인딩 | SDL 2.30.12 공식 소스, 제한된 로컬 수정 포함 | Zlib. [SDL 고지](platforms/android/SDL-LICENSE.txt), [파일별 출처·해시](platforms/android/verification/sdl-java-provenance.json), [수정 패치](platforms/android/verification/sdl-android-permissions.patch) |

SDL Android Java의 로컬 수정은 선택적 하드웨어 기능의 권한 확인과 Android 13 이상의 USB 수신기 등록 플래그 처리입니다. 이 사본을 수정하지 않은 공식 배포본으로 표시하지 않습니다. 앱 전용 `GameSurface`는 별도로 SDL의 센서 등록 확장 지점을 차단하며 기존 SDL 바인딩의 고지를 유지합니다.

## 빌드 중 가져오는 의존성

| 구성요소 | 출처·버전 | 보존한 고지 |
| --- | --- | --- |
| SDL | [공식 릴리스](https://www.libsdl.org/release/), 네이티브·Android 2.30.12 | [Zlib 고지](licenses/SDL2-LICENSE.txt) |
| SDL_mixer | [공식 릴리스](https://www.libsdl.org/projects/SDL_mixer/release/), 네이티브·Android 2.8.1 | [Zlib 고지](licenses/SDL2_mixer-LICENSE.txt) |
| TiMidity | SDL_mixer에 포함된 MIDI 디코더. 이 프로젝트의 데스크톱 설정에서 사용 | [Artistic License](licenses/TiMidity-COPYING.txt), [저작권·변경 안내](licenses/TiMidity-README.txt) |
| dr_libs | SDL_mixer에 포함된 오디오 디코더 코드 | [MIT-0·Public Domain 고지](licenses/dr_libs-LICENSE.txt) |

내려받은 전체 SDL 캐시는 공개 소스에 포함하지 않습니다. 소스 배포본 안의 추가 고지도 함께 유지해야 합니다. 웹 빌드의 SDL 포트·런타임은 사용한 Emscripten SDK에서 제공되며 SDK와 포트의 라이선스·버전은 그 배포본을 따릅니다. Gradle 플러그인·AndroidX annotation·JUnit 등 빌드/시험 도구는 해당 배포처에서 받으며 이 저장소의 소스 라이선스로 재허가하지 않습니다.

## 글꼴과 SoundFont

### Noto Sans KR

`assets/fonts/NotoSansKR.ttf`는 Google Fonts의 Noto Sans KR 자료에서 정적 굵기 400과 필요한 문자 범위로 변환한 사본입니다. [출처·입출력 SHA256](assets/fonts/SOURCE.json)과 [SIL Open Font License 1.1 전체 문구](assets/fonts/OFL.txt)를 함께 보존합니다. 보존된 저작권 문구는 `Copyright 2014–2021 Adobe`, Reserved Font Name `Source`입니다. 글꼴을 수정·재배포할 때에는 별도 라이선스와 이름 관련 조건을 확인해야 합니다.

### TimGM6mb SoundFont

`assets/audio/TimGM6mb.sf2`는 Debian `timgm6mb-soundfont 1.3-5`에서 얻은 변경하지 않은 전체 SoundFont입니다. 출처 메타데이터에는 Copyright 2004 Tim Brechbill, 2010 David Bolton 및 `GPL-2.0-only`로 기록되어 있습니다.

다음 파일을 SoundFont와 함께 유지합니다.

- [출처·패키지·자산 해시](assets/audio/SOURCE.json)
- [저작권 고지](assets/audio/copyright)
- [GPL version 2 전체 문구](assets/audio/GPL-2.txt)
- [사용 방식 안내](assets/audio/README.txt)

원본 SoundFont의 GPL-2.0-only와 Debian 패키징 파일에 별도로 기재된 GPL-3+ 표기는 구분합니다. SoundFont의 라이선스는 원본 게임의 음악·효과음에 대한 권리를 부여하지 않습니다.

## 선택 고화질 표시 자료의 제작 도구

Windows·웹 패키지의 선택 자료 `assets/hq/pinball-2x.hqpb`는 원본 게임 그림에서 추출한 표시용 RGB 414개를 처리한 pack입니다. 실사용본은 Real-ESRGAN-ncnn-vulkan의 `realesr-animevideov3` native 2배 처리와 Pillow의 nearest·Lanczos 및 지정 혼합으로 제작했습니다. 모델·실행 도구·제작 중간 자료는 게임 런타임에 포함하지 않습니다.

- Real-ESRGAN-ncnn-vulkan 및 realsr-ncnn-vulkan 기반 부분: MIT. [전체 고지](licenses/Real-ESRGAN-ncnn-vulkan-LICENSE.txt).
- 상위 Real-ESRGAN: BSD 3-Clause. [전체 고지](licenses/Real-ESRGAN-LICENSE.txt).

원본 게임 자료의 권리는 원저작자에게 남으며, AI 처리나 제작 도구의 라이선스가 원본 자료의 새로운 배포 권한을 부여한다고 주장하지 않습니다. pack의 출처·해시와 적용 범위는 [HQ_NOTICES.md](HQ_NOTICES.md)를 확인하세요.

## 포함되지 않는 게임 자료

원본 Windows/Full Tilt 게임의 DAT·WAV·MID·BMP 등 외부 게임 자료, 원본 실행 파일과 개인 검증 자료는 공개 소스에 포함하지 않습니다. 사용자는 자신이 사용할 권한을 가진 게임 자료를 로컬에서 준비해야 합니다. 해당 자료를 포함해 생성한 APK·웹 `.data`·실행 패키지의 배포 권한은 공개 소스의 MIT 라이선스로 확보되는 것이 아닙니다.
