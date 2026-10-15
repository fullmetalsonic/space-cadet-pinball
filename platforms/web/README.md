# 웹 셸과 패키징

게임·설정 창·터치 버튼·게임 규칙은 공유 C++ 코어가 그립니다. 이 폴더는 브라우저 수명주기, 저장, 화면 배치와 오프라인 파일 세트를 담당합니다. canvas CSS는 stage 크기와 일치시키고 관찰 이벤트를 다음 프레임의 resize 한 번으로 합칩니다. SDL의 기존 CSS resize 경로가 창·renderer·ImGui 크기를 갱신하며, 보드 비율은 공유 코어가 유지합니다. canvas backing 속성을 직접 바꾸지 않습니다.

## 코어 계약

- Emscripten 결과: `SpaceCadetPinball.js`, `.wasm`, `.data`. 게임 자료와 한글 글꼴은 `.data`에 포함합니다.
- 런타임: `-lidbfs.js`, `-sFORCE_FILESYSTEM=1`, `-sDYNAMIC_EXECUTION=0`, `-sEXPORTED_RUNTIME_METHODS=FS,addRunDependency,removeRunDependency`, `-sEXIT_RUNTIME=0`.
- `preRun`에서 `/persistent`에 IDBFS를 마운트하고 `syncfs(true)` 완료 후 실행 의존성을 해제합니다. C++ 설정 읽기는 이 이후에 시작해야 합니다.
- C++ 초기화 성공 후 `globalThis.pinballCoreReady?.()`를 호출합니다. 파일 손상 복구 안내는 `globalThis.pinballStorageError?.()`를 호출합니다.
- 저장 파일 기록 후 `globalThis.pinballPersistenceFlush?.()`를 호출합니다. 이 함수는 IndexedDB 쓰기를 직렬화하고 실패 시 사용자 재시도 전에는 반복하지 않습니다.
- 노출 함수: `platform_cancel_inputs()`, `platform_notify_pause(paused, session)`, `platform_flush_settings()`, `platform_activate_audio()`. 수명주기에서 `session=0`을 전달합니다. 실제 값 의미는 코어 계약에 맞춰 통합합니다.
- 초기 저장 복원 실패 때 사용자가 ‘저장 없이 실행’을 선택하면 이번 실행의 IndexedDB 쓰기를 중단하여 기존 기록을 보호합니다.

## 생성과 표적 검사

```text
node platforms/web/package.mjs --build-dir <Emscripten 출력 폴더> --output <새 웹 패키지 폴더> --source-version <소스 버전>
node --test platforms/web/tests/*.test.mjs
```

출력은 다른 버전으로 덮어쓰지 않습니다. 각 패키지는 소스 버전과 전체 파일 SHA256을 기록한 `asset-manifest.json`, 해당 버전의 `sw.js`, 설치용 192/512px PNG 아이콘을 포함합니다. 서비스 워커는 모든 파일 크기·해시가 일치한 세트만 설치합니다. 업데이트는 사용자 선택 후 저장 성공을 확인하고 적용합니다. 기존 캐시와 IDBFS 저장소는 삭제하지 않습니다. 웹 안내 언어는 공유 코어의 한국어/영어 설정을 따릅니다.

HTTP는 localhost에서 시험하며 외부 게시와 배포는 별도 승인 대상입니다. 첫 온라인 실행의 ‘오프라인 실행 준비가 끝났습니다’ 안내 후 오프라인 실행을 시험합니다. 서비스 워커와 IndexedDB를 지원하는 보안 컨텍스트가 필요합니다.

Node 검사는 저장 직렬화·오류 회복, 화면 비율, 패키지 무결성, SW 런타임의 오프라인 요청·불완전 업데이트 거부를 확인합니다. 테스트의 게임 파일과 브라우저 API 저장소는 시험용이므로 실제 WASM 실행, 브라우저 오프라인, 최고점·언어 유지, 오디오, 터치와 `WEB-01` 전체 합격을 증명하지 않습니다. 실제 브라우저 검증은 메인이 같은 브라우저 세션에서 수행합니다.
