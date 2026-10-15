# 고화질 자료

Windows·웹의 `옵션 → 그래픽 → 표시 모드 → 고화질`은 원본 그림을 2배로 처리한 별도 표시 자료를 사용합니다. 기본값은 원본입니다. 모드를 바꾸면 게임이 일시정지되므로 직접 재개하세요. 이번 배포에는 새 네온 스타일 시안을 포함하지 않습니다.

## 실행에 필요한 파일

선택한 [Release](https://github.com/fullmetalsonic/space-cadet-pinball/releases/tag/v0.1.1-preview.1)의 Windows ZIP에서 `assets/hq/pinball-2x.hqpb`를 가져와 같은 상대 경로에 둡니다. `HQ_NOTICES.md`와 동봉한 라이선스 고지도 함께 보존합니다. pack은 소스 브랜치에 포함되지 않습니다. Windows·웹 CMake는 파일이 있을 때만 포함하며, 없거나 DAT와 일치하지 않으면 원본 모드를 사용할 수 있습니다.

배포 pack은 `PBHQv1`, 배율 2, 표시 자료 414개입니다. SHA256은 `645d5420d3622689540c1bddf31f298792367cc83150d247695bbefd6f94d48e`입니다. PNG, 모델과 제작 도구는 게임 실행에 필요하지 않습니다.

## pack 제작 개요

자신이 사용할 권한을 가진 `PINBALL.DAT`를 준비합니다. 엔진의 정규화된 GroupData·팔레트 경로로 추출하는 선택 도구는 다음과 같이 빌드합니다. 기존 Windows 빌드 구성이 필요하며, 아래 명령은 추가 target만 빌드합니다.

```powershell
cmake -S engine -B build/windows -DPINBALL_BUILD_HQ_ASSET_TOOLS=ON
cmake --build build/windows --target pinball_export_hq_assets
```

`pinball_export_hq_assets`의 인수는 `PINBALL.DAT 경로`, `비어 있는 기존 출력 폴더`, `DAT의 소문자 SHA256`입니다. 추출기는 정규화 색상·깊이 자료와 `inventory.json`을 출력합니다. `scripts/hq/pack.py`에는 각 자료의 `groupId`, `resolution`, `width`, `height`, `xPosition`, `yPosition`, `key`, `originalArgbSha256` 및 원본 DAT의 `sourceSha256`을 기록한 `manifest.json`과 `<key>.bgra32le`, `<key>.png`가 필요합니다. 이것은 추출기의 inventory와 검증된 정규화 BGRA 파일을 바탕으로 준비합니다. 중간 자료와 원시 기록은 공개 소스에 포함하지 않습니다.

Pillow를 사용할 수 있는 Python 환경에서, 동일한 키와 정확히 2배인 RGB PNG를 별도 폴더에 준비한 뒤 실행합니다. 출력 파일은 새 경로여야 합니다.

```powershell
python scripts/hq/pack.py --source assets/hq/source `
  --scale 2 --images assets/hq/selected-2x `
  --out assets/hq/pinball-2x.hqpb
Get-FileHash assets/hq/pinball-2x.hqpb -Algorithm SHA256
```

pack 생성기는 원래 크기·위치·정규화 BGRA 해시와 payload 길이·전체 footer 해시를 확인하고, 옆에 `.hqpb.json`을 만듭니다. 임의의 확대 이미지가 배포 pack과 같은 품질이나 해시를 갖는다는 뜻은 아닙니다. 배포 자료는 글자 nearest, 공·로고 Lanczos, 조명·플리퍼 anime2x, 나머지 지정 혼합으로 처리한 고정 선택본입니다.

## 표시와 게임 동작의 경계

pack에는 RGB만 들어갑니다. 원본 DAT·bitmap 크기·anchor·충돌·마스크·깊이 정보는 바꾸지 않습니다. 일반 스프라이트는 원래 사각형 범위와 `dstDepth >= srcDepth` 규칙을 사용하므로 색상 0을 일괄 투명 처리하지 않습니다. 공은 원본의 색상 0 제외와 엄격한 깊이 비교를 유지합니다. AI 처리에 마스크와 깊이를 입력하지 않았습니다.

게임 자료의 권리는 원저작자에게 남으며 엔진 MIT나 제작 도구의 라이선스가 원본 자료의 별도 권리를 부여하지 않습니다. 관련 고지는 패키지의 `HQ_NOTICES.md`와 [제3자 고지](../THIRD_PARTY_NOTICES.md)를 확인하세요.
