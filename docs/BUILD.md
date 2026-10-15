# 소스 빌드

공유 C++ 코어를 Windows 실행 파일, 브라우저용 WASM, Android APK로 빌드합니다. 아래 명령은 저장소 루트에서 시작하는 PowerShell 예시입니다. 도구는 사용 환경의 `PATH`와 환경변수로 지정합니다.

이 문서는 현재 CMake·Gradle 설정과 기존 성공 명령을 대조해 작성했습니다. 문서 작성 중 새 빌드나 깨끗한 PC에서의 재현 시험은 실행하지 않았습니다.

## 게임 자료 준비

**원본 게임 자료는 공개 저장소에 포함되지 않습니다.** 직접 사용 권한을 확보한 게임 자료를 로컬에 준비해야 실행할 수 있습니다. 공개 소스의 라이선스는 원본 게임 자료에 대한 권리를 부여하지 않습니다.

```powershell
pwsh -File ./scripts/Stage-GameAssets.ps1 -ReferenceDir '/path/to/your/game-data'
```

스크립트는 `PINBALL.DAT`가 있는 폴더에서 `.dat`, `.wav`, `.mid`, `.bmp`, `.inf`만 `assets/game/`으로 복사합니다. 기존 파일의 내용이 다르면 복사 전에 중단하고, 복사 후 SHA256을 비교합니다. 실행 파일은 복사하지 않습니다. 생성한 게임 자료와 이를 포함한 빌드 결과를 공개 저장소에 추가하지 마세요.

```text
assets/
  game/                 # 사용자가 로컬에서 준비한 DAT/WAV/MID 등
    PINBALL.DAT
  fonts/
    NotoSansKR.ttf
    OFL.txt
    SOURCE.json
  audio/
    TimGM6mb.sf2
    copyright
    GPL-2.txt
    SOURCE.json
    README.txt
```

글꼴과 SoundFont의 라이선스·출처는 [제3자 고지](../THIRD_PARTY_NOTICES.md)를 참조하세요. 한국어 글꼴은 엔진 초기화에 필요하며, Android의 자료 매니페스트 생성은 게임 DAT·글꼴·SoundFont가 빠지면 실패합니다. 웹 빌드도 이 자료를 `.data`에 포함합니다.

네이티브·웹 CMake의 자료 위치는 `-DPINBALL_ASSETS_DIR=<절대 자료 경로>`로 바꿀 수 있습니다. Android의 자산 수집은 현재 저장소 루트 `assets/`를 사용하므로 같은 옵션만으로 Android 자산 위치가 바뀌지는 않습니다.

## 도구와 의존성

| 대상 | 사용한 도구·설정 |
| --- | --- |
| 공통 | CMake 3.22 이상, C++11 컴파일러, Ninja |
| Windows | LLVM-MinGW Clang 22.1.8, CMake, Ninja |
| 웹 | Emscripten 4.0.23의 활성화된 환경, CMake, Ninja, Node.js |
| Android | JDK 21로 검증, Gradle 8.9, AGP 8.7.3, SDK API 35, CMake 3.22.1, NDK 28.2.13676358 |

Android의 최소 API는 26, 대상 API는 35이며 ABI는 `arm64-v8a`입니다. JDK 17 또는 21을 사용할 수 있습니다. Gradle wrapper는 포함하지 않으므로 Gradle 8.9를 별도로 준비합니다.

네이티브 빌드의 기본 `PINBALL_FETCH_DEPENDENCIES=ON`은 SDL 2.30.12와 SDL_mixer 2.8.1의 지정된 릴리스를 가져옵니다. 최초 구성에는 네트워크가 필요할 수 있습니다. Android에서는 공식 ZIP의 SHA256을 확인하는 다음 스크립트로 로컬 소스를 준비할 수 있습니다.

```powershell
pwsh -File ./platforms/android/Prepare-Sdl.ps1
```

생성되는 `platforms/android/vendor/`는 로컬 캐시입니다. Android CMake는 이 디렉터리가 있으면 재사용합니다. 직접 설치한 SDL CMake 패키지를 사용하는 네이티브 빌드는 `-DPINBALL_FETCH_DEPENDENCIES=OFF`와 해당 패키지 검색 경로가 필요합니다. 웹 빌드는 네이티브 FetchContent 대신 해당 Emscripten SDK의 SDL 포트를 사용합니다.

## Windows

LLVM-MinGW의 `bin`과 CMake·Ninja가 `PATH`에 있는 PowerShell에서 실행합니다. 다른 Clang 배포판이 먼저 검색되지 않도록 컴파일러를 확인하세요.

```powershell
x86_64-w64-mingw32-clang++ --version
cmake -S engine -B build/windows -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_C_COMPILER=x86_64-w64-mingw32-clang `
  -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-clang++ `
  -DPINBALL_BUILD_TESTS=ON
cmake --build build/windows --parallel 2
```

출력은 `build/windows/bin/SpaceCadetPinball.exe`입니다. CMake는 SDL DLL 두 개와 `assets/`를 실행 파일 옆으로 복사합니다. 실행할 때는 이 디렉터리 구성을 함께 유지합니다.

```powershell
& ./build/windows/bin/SpaceCadetPinball.exe
& ./build/windows/bin/pinball_runtime_checks.exe "$PWD/assets" --session-only
& ./build/windows/bin/pinball_runtime_checks.exe "$PWD/assets" --ime-only
```

공유 코어의 두 표적 검사는 별도 숨은 창과 임시 저장 파일을 사용합니다. 첫 검사는 세션 갱신·일시정지·키/화면 흔들기, 두 번째는 이름 입력·로컬 기록 경계를 확인합니다. 전체 런타임 시험이 필요하면 `ctest --test-dir build/windows --output-on-failure`를 사용할 수 있습니다. 이 검사들은 모든 실제 장치의 플레이·오디오 확인을 대신하지 않습니다.

## 웹

Emscripten SDK 환경을 활성화하고 저장소 루트에서 실행합니다.

```powershell
emcmake cmake -S engine -B build/web -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/web --parallel 2
node --test platforms/web/tests/*.test.mjs
$sourceVersion = (git rev-parse --short HEAD).Trim()
node platforms/web/package.mjs `
  --build-dir build/web/bin `
  --output build/web-package `
  --source-version $sourceVersion
```

코어 출력은 `build/web/bin/SpaceCadetPinball.js`, `.wasm`, `.data`입니다. 패키징의 `--output`은 새 빈 디렉터리를 사용하세요. 다른 버전이나 손상된 내용이 이미 있으면 중단됩니다. 패키지에는 파일별 SHA256을 가진 `asset-manifest.json`과 버전별 서비스 워커가 생성됩니다.

```powershell
python -m http.server 8000 --bind 127.0.0.1 --directory build/web-package
```

브라우저에서 `http://127.0.0.1:8000/`을 엽니다. IndexedDB와 서비스 워커를 지원해야 하며, 오디오는 사용자 동작 이후 활성화됩니다. 오프라인 실행은 첫 온라인 실행에서 준비 완료 안내를 확인한 뒤 시험합니다. `.data`에는 사용자가 준비한 게임 자료가 들어가므로 이 패키지를 소스와 함께 공개하지 마세요.

## Android

Android SDK에 API 35 플랫폼·빌드 도구·NDK `28.2.13676358`·CMake `3.22.1`을 준비하고, `ANDROID_HOME`을 SDK 디렉터리로 설정합니다. `JAVA_HOME`과 `PATH`에는 JDK와 Gradle 8.9를 지정합니다. 개인 `local.properties`와 서명 키는 커밋하지 않습니다.

현재 Gradle의 NDK 기본 경로는 개발 환경 전용이므로 **항상 `pinballNdkPath`를 자신의 설치 위치로 지정**합니다. 제품 소스 수정은 필요 없습니다.

```powershell
$ndkPath = Join-Path $env:ANDROID_HOME 'ndk/28.2.13676358'
$buildRoot = Join-Path (Get-Location) 'build/android'
Push-Location platforms/android
try {
  gradle --console=plain `
    "-PpinballNdkPath=$ndkPath" `
    "-PpinballBuildRoot=$buildRoot" `
    :app:testDebugUnitTest :app:assembleDebug :app:lintDebug
} finally {
  Pop-Location
}
```

처음에는 Gradle 플러그인과 Java 의존성을 받아야 하므로 네트워크가 필요할 수 있습니다. 모든 의존성이 캐시에 있는 경우에만 `--offline`을 추가합니다. `pinballBuildRoot`는 출력·네이티브 중간 파일 위치만 바꾸며, 게임 자료는 루트 `assets/`에서 읽습니다.

APK는 `build/android/app/outputs/apk/debug/app-debug.apk`에 생성됩니다. 이는 로컬 디버그 서명을 사용하는 APK입니다. 기존 설치를 업데이트하려면 동일한 서명 키가 필요합니다. 이 저장소는 개인 디버그 키나 배포용 서명 키를 제공하지 않습니다.

일반 오디오 진단은 기본 OFF입니다. 일반 APK에는 `-PpinballAudioDiagnostics=true`를 전달하지 않습니다. 휴대폰 센서 흔들기 기능은 제거되었으며 화면 흔들기 버튼·기본키와 게임의 Tilt 제재는 유지됩니다.

선택한 SDK 빌드 도구의 `apksigner`와 `zipalign`을 `PATH`에 두고 패키지 형식을 확인할 수 있습니다.

```powershell
python platforms/android/verification/verify_apk.py build/android/app/outputs/apk/debug/app-debug.apk
apksigner verify --verbose --print-certs build/android/app/outputs/apk/debug/app-debug.apk
zipalign -c -P 16 -v 4 build/android/app/outputs/apk/debug/app-debug.apk
```

정렬·서명 검사와 실제 Android 입력·복귀·소리 검증은 구분합니다. 이 문서의 명령에는 설치·기기 초기화·외부 게시가 포함되지 않습니다.
