# 고화질 표시 자료 출처와 고지

`assets/hq/pinball-2x.hqpb`는 원본 `PINBALL.DAT`의 정규화된 표시 자료를 2배로 처리한 RGB pack입니다. 대상 DAT의 SHA256은 `42defd5d2a339a3953fe643716aff4e35b2cea7a3e54505c82d1e78d5325bc04`, pack의 SHA256은 `645d5420d3622689540c1bddf31f298792367cc83150d247695bbefd6f94d48e`입니다. 표시 자료 414개를 포함하며 원본 크기·위치·마스크·깊이·충돌 자료는 변경하지 않습니다.

원본 그림·문자·게임 자료의 권리는 원저작자에게 남습니다. 추출·AI 처리 또는 아래 도구의 라이선스가 원본 자료에 새로운 배포 권한을 부여한다고 주장하지 않습니다. 프로젝트 코드의 MIT 라이선스도 원본 게임 자료의 권리와 별개입니다.

정규화 추출은 [SpaceCadetPinball](https://github.com/k4zmu2a/SpaceCadetPinball)의 GroupData·팔레트 경로를 사용했습니다. 엔진은 MIT, Copyright © 2020–2021 Andrey Muzychenko입니다. 소스의 `engine/LICENSE` 또는 실행 패키지의 `licenses/SpaceCadetPinball-LICENSE.txt`에 전체 고지를 보존합니다.

실사용 RGB는 [Real-ESRGAN-ncnn-vulkan](https://github.com/xinntao/Real-ESRGAN-ncnn-vulkan)의 `realesr-animevideov3` native 2배 처리와 Pillow 12.3.0의 nearest·Lanczos 및 지정 혼합으로 제작했습니다. 글자 106개는 nearest, 공 7개와 로고 배경 1개는 Lanczos, 조명·플리퍼 161개는 anime2x, 나머지 139개는 anime 75%·Lanczos 25%를 사용했습니다. 일부 원본 회사명 영역은 원본의 Lanczos 결과로 보존했습니다. 모델과 실행 도구는 게임 패키지에 포함하지 않습니다.

- Real-ESRGAN-ncnn-vulkan 및 realsr-ncnn-vulkan 기반 부분: MIT, Copyright © 2021 Xintao Wang 및 © 2019 nihui. 전체 고지: [licenses/Real-ESRGAN-ncnn-vulkan-LICENSE.txt](licenses/Real-ESRGAN-ncnn-vulkan-LICENSE.txt).
- 상위 Real-ESRGAN: BSD 3-Clause, Copyright © 2021 Xintao Wang. 전체 고지: [licenses/Real-ESRGAN-LICENSE.txt](licenses/Real-ESRGAN-LICENSE.txt).

이 고지는 도구·모델·원자료에 적용되는 권리를 넓혀 해석하지 않습니다. 다른 구성요소의 고지는 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)를 확인하세요.
