# Space Cadet Pinball — Windows · Web · Android

Windows XP의 **3D 핀볼: Space Cadet**을 현대 Windows, 브라우저, Android에서 실행하기 위한 복원 프로젝트입니다. [k4zmu2a/SpaceCadetPinball](https://github.com/k4zmu2a/SpaceCadetPinball)의 C++ 코어를 기반으로 한국어 지원, 터치 조작, 이름 입력, 저장과 플랫폼별 수명주기를 보완했습니다. Microsoft 공식 제품이 아닙니다.

## 현재 상태

- Windows 실행형, WebAssembly 브라우저판, Android arm64 APK를 로컬에서 빌드하고 대표 사용자 흐름을 검증했습니다.
- 세부 기획 v1.2의 유효 완료 조건 **21개 중 20개 PASS**, **소리 최종 확인 1개 부분 완료**입니다. 전체 완료판정은 아직 보류입니다. [검증 범위와 한계](docs/VALIDATION.md)를 참고하세요.
- 폰 기울기 센서와 관련 옵션은 제거했습니다. **화면 좌·우·앞 흔들기 버튼**과 원래 게임의 Tilt 반칙은 유지합니다.
- 최고 점수·이름·설정은 각 기기 또는 브라우저에 저장합니다. 공용 인터넷 순위표와 계정 동기화는 포함하지 않습니다.

## 바로 플레이 · 다운로드

**[브라우저에서 바로 플레이](https://fullmetalsonic.github.io/space-cadet-pinball/)** — 설치 없이 실행합니다. 최고 점수와 설정은 각 브라우저에 저장됩니다.

[Windows · Web · Android 다운로드](https://github.com/fullmetalsonic/space-cadet-pinball/releases/tag/v0.1.0-preview.1)에서 파일을 받을 수 있습니다.

- **Windows:** `windows-x64.zip` 전체 압축 해제 후 `SpaceCadetPinball.exe` 실행. DLL과 `assets/`를 함께 유지합니다.
- **Android:** arm64 APK를 사용합니다. Android 8.0 이상이 필요합니다.
- **웹:** 위 플레이 링크로 바로 접속하거나 `web.zip`으로 로컬에서 실행합니다. 로컬 실행에는 정적 HTTP 서버가 필요하며, ZIP의 `README-START.txt`에 실행 방법이 있습니다.

## 소스에서 빌드할 때

`main` 브랜치에는 빌드 가능한 소스를 두고, `gh-pages` 브랜치에는 배포용 웹 빌드를 둡니다. 소스 브랜치에는 XP 원본 실행 파일과 `assets/game/` 자료를 포함하지 않습니다. Release 실행 패키지와 웹 빌드에는 게임 자료가 포함됩니다. 소스에서 직접 빌드할 때는 사용할 권한이 있는 게임 자료를 로컬 `assets/game/`에 준비하세요.

```powershell
./scripts/Stage-GameAssets.ps1 -ReferenceDir 'C:/your-local-pinball-data'
```

스크립트는 로컬 자료만 복사하며 원본 실행 파일을 복사하거나 인터넷에서 다운로드하지 않습니다. 이미 존재하는 다른 내용의 파일은 덮어쓰지 않습니다. 자세한 도구와 명령은 [빌드 방법](docs/BUILD.md)에 있습니다.

## 구성

| 경로 | 내용 |
| --- | --- |
| `engine/` | 공유 C++ 게임 코어와 코어 검사 |
| `platforms/web/` | 브라우저 입력·저장·오프라인 패키징 |
| `platforms/android/` | SDL 기반 Android 앱·IME·화면·수명주기 |
| `assets/fonts/`, `assets/audio/` | 출처와 라이선스를 포함한 글꼴·SoundFont |
| `docs/` | [기획 v1.2](docs/SPEC.md), [검증 요약](docs/VALIDATION.md), [빌드 방법](docs/BUILD.md) |

## 라이선스

프로젝트 코드의 기본 라이선스는 [MIT](LICENSE)이며 원본 프로젝트의 저작권 고지를 보존합니다. 외부 코드·글꼴·SoundFont에는 각각의 라이선스가 적용됩니다. 특히 `TimGM6mb.sf2`는 **GPL-2.0-only**, Noto 글꼴은 **SIL OFL 1.1**입니다. [외부 구성요소 고지](THIRD_PARTY_NOTICES.md)를 확인하세요. 이 라이선스는 별도로 필요한 원본 게임 자료에 대한 권리를 부여하지 않습니다.
