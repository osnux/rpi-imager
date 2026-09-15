# TOPST SBC eMMC FWDN 플래싱 지원 구현 완료 보고서

## 1. 개요 및 배경

`rpi-imager`는 본래 SD 카드나 USB 드라이브와 같은 블록 스토리지에 라즈베리파이의 raw 이미지를 굽기 위한 도구입니다. 
하지만 개발 중이신 **TOPST Single Board Computer (SBC)**는 다음 두 가지 큰 차이점을 가지고 있습니다:
1. **내장 eMMC 및 FWDN 모드**: Mass Storage 블록 장치가 아닌 Telechips FWDN (Firmware Download) 프로토콜(USB VID `0x140E`) 또는 USB 시리얼(COM)을 통해 보드에 연결됩니다.
2. **독특한 이미지 형식 (`.fai`)**: 일반적인 단일 파티션 이미지(raw .img/.iso)가 아닌, Telechips GPT 파티션 결합 포맷인 `.fai` (Firmware Artifact Image) 파일입니다.

[topst-development/tools](https://github.com/topst-development/tools)를 심층 분석하고, `rpi-imager`의 최신 비동기 파이프라인(`rpiboot` 및 `FastbootFlashThread` 패턴)을 벤치마킹하여 **TOPST eMMC 플래싱을 완벽히 지원하는 커스텀 이미저 아키텍처**를 완성하였습니다.

---

## 2. 주요 구현 내용

### 2.1 TOPST FWDN 장치 스캐너 (`FwdnScanner`)
- [fwdn_scanner.h](file:///c:/Users/wjmoh-home/Documents/work/rpi-imager/src/fwdn/fwdn_scanner.h) / [fwdn_scanner.cpp](file:///c:/Users/wjmoh-home/Documents/work/rpi-imager/src/fwdn/fwdn_scanner.cpp)
- **USB 자동 감지**:
  - `libusb`를 통해 Telechips USB 장치(Vendor ID: `0x140E`)를 백그라운드에서 실시간 스캔 $\to$ **TOPST D3 (Dolphin 3)** 감지.
  - Windows 레지스트리(`HARDWARE\DEVICEMAP\SERIALCOMM`) 및 Linux `/dev/ttyUSB*`, `/dev/ttyACM*` 스캔 $\to$ **TOPST AI-G** 시리얼 포트 감지.
- **개발/테스트용 에뮬레이션 지원**:
  - 실제 보드가 없는 환경에서도 UI와 흐름을 검증할 수 있도록 `TOPST_FWDN_FORCE_D3=1`, `TOPST_FWDN_FORCE_AI=1` 환경변수 지원.

### 2.2 스토리지 모델 및 아이템 확장
- [drivelist.h](file:///c:/Users/wjmoh-home/Documents/work/rpi-imager/src/drivelist/drivelist.h) / [drivelistitem.h](file:///c:/Users/wjmoh-home/Documents/work/rpi-imager/src/drivelistitem.h) / [drivelistitem.cpp](file:///c:/Users/wjmoh-home/Documents/work/rpi-imager/src/drivelistitem.cpp)
- [drivelistmodel.h](file:///c:/Users/wjmoh-home/Documents/work/rpi-imager/src/drivelistmodel.h) / [drivelistmodel.cpp](file:///c:/Users/wjmoh-home/Documents/work/rpi-imager/src/drivelistmodel.cpp) / [drivelistmodelpollthread.cpp](file:///c:/Users/wjmoh-home/Documents/work/rpi-imager/src/drivelistmodelpollthread.cpp)
- `Drivelist::DeviceDescriptor`와 `DriveListItem`에 `isFwdn`, `fwdnBoardType`, `fwdnPort` 속성 추가.
- 폴링 스레드(`DriveListModelPollThread`)에서 FWDN 장치를 스캔하여 스토리지 목록에 `TOPST D3 (eMMC via FWDN)` 또는 `TOPST AI-G (eMMC via FWDN - COMx)` 항목 자동 노출.

### 2.3 TOPST 플래시 실행 엔진 (`FwdnFlashThread`)
- [fwdnflashthread.h](file:///c:/Users/wjmoh-home/Documents/work/rpi-imager/src/fwdn/fwdnflashthread.h) / [fwdnflashthread.cpp](file:///c:/Users/wjmoh-home/Documents/work/rpi-imager/src/fwdn/fwdnflashthread.cpp)
- 번들링된 텔레칩스 검증 도구(`fwdn`, `fwdn_ai`, `VtcUsbPort.dll`)를 `QProcess`를 통해 단계별 비동기 실행:
  - **1단계 (Bootstrap)**: `--fwdn boot-firmware/fwdn.json`으로 RAM에 FWDN ROM 업로드
  - **2단계 (Low Format)**: `--storage emmc --low-format`으로 eMMC 초기화
  - **3단계 (Bootloader)**: `-w boot-firmware/boot.json`으로 부트 펌웨어 기록
  - **4단계 (OS Flashing)**: `-w <image>.fai --storage emmc --area user`로 최종 시스템 파티션 플래싱
- 프로세스 표준 출력(stdout)을 파싱하여 실시간 진행률(Progress bar) 및 단계별 설명(`preparationStatusUpdate`, `writing`, `finalizing`)을 QML UI로 스트리밍.
- 유저 취소(`cancel()`) 시 하위 프로세스 안전 종료 지원.

### 2.4 코어 및 UI/QML 계층 통합
- [imagewriter.h](file:///c:/Users/wjmoh-home/Documents/work/rpi-imager/src/imagewriter.h) / [imagewriter.cpp](file:///c:/Users/wjmoh-home/Documents/work/rpi-imager/src/imagewriter.cpp):
  - `setFwdnDevice()`, `isFwdnDevice()`, `fwdnBoardType()`, `fwdnPort()` 메서드 추가.
  - `startWrite()` 실행 시 FWDN 디바이스 여부를 판별하여 `FwdnFlashThread` 구동.
- [CommonStrings.qml](file:///c:/Users/wjmoh-home/Documents/work/rpi-imager/src/CommonStrings.qml):
  - 파일 브라우저 이미지 필터 목록에 `*.fai` 추가 (`["*.fai", "*.img", "*.zip", ...]`).
- [StorageSelectionStep.qml](file:///c:/Users/wjmoh-home/Documents/work/rpi-imager/src/wizard/StorageSelectionStep.qml):
  - FWDN 디바이스 전용 전용 뱃지 및 아이콘 렌더링.
  - 타겟 클릭 시 `imageWriter.setFwdnDevice()` 호출 연동.

### 2.5 빌드 시스템 및 리소스 구성
- [CMakeLists.txt](file:///c:/Users/wjmoh-home/Documents/work/rpi-imager/src/CMakeLists.txt):
  - 신규 소스 컴파일 목록 등록.
  - 빌드 후 `src/tools` 디렉토리를 실행 파일 출력 디렉토리로 복사하는 post-build 타겟 추가.
- [99-topst.rules](file:///c:/Users/wjmoh-home/Documents/work/rpi-imager/src/linux/99-topst.rules):
  - Linux 환경에서 root 권한 없이 플래싱할 수 있도록 Telechips VID `140E` 및 tty USB udev 룰 추가.

---

## 3. 전체 플래싱 흐름도

```mermaid
sequenceDiagram
    autonumber
    actor User as 사용자
    participant UI as QML Wizard UI
    participant Scanner as FwdnScanner
    participant IW as ImageWriter
    participant Thread as FwdnFlashThread
    participant FWDN as fwdn / fwdn_ai CLI

    Note over Scanner,UI: 백그라운드 USB/Serial 폴링
    Scanner->>UI: TOPST Board (eMMC via FWDN) 장치 목록 표시
    User->>UI: 1) OS 선택: custom image (.fai)
    User->>UI: 2) 스토리지 선택: TOPST eMMC (FWDN)
    UI->>IW: setFwdnDevice(device, boardType, port)
    User->>UI: 3) "Next" -> "Write" 클릭
    UI->>IW: startWrite()
    IW->>Thread: FwdnFlashThread 생성 및 start()
    
    rect rgb(240, 248, 255)
    Note over Thread,FWDN: 4단계 플래싱 파이프라인
    Thread->>FWDN: Step 1. Bootstrap (--fwdn fwdn.json)
    FWDN-->>Thread: RAM 업로드 완료
    Thread->>FWDN: Step 2. Low-format (--low-format)
    FWDN-->>Thread: eMMC 초기화 완료
    Thread->>FWDN: Step 3. Write Boot FW (-w boot.json)
    FWDN-->>Thread: 부트로더 플래싱 완료
    Thread->>FWDN: Step 4. Write OS Image (-w target.fai)
    loop 진행률 파싱
        FWDN-->>Thread: stdout progress (%)
        Thread->>>UI: writeProgress(now, total)
    end
    end

    Thread->>IW: emit success()
    IW->>UI: Write Complete 다이얼로그 표시
```

---

## 4. 검증 및 테스트 방법

### 4.1 가상 에뮬레이션 테스트
실제 보드가 연결되지 않은 상태에서도 환경변수를 설정하여 UI 표시 및 동작을 확인할 수 있습니다:
```powershell
# PowerShell 환경에서
$env:TOPST_FWDN_FORCE_D3 = "1"
# 또는
$env:TOPST_FWDN_FORCE_AI = "1"
```
실행 후 스토리지 선택 화면(`StorageSelectionStep`)에 **`TOPST D3 (eMMC via FWDN) [Emulated]`** 항목이 표시되는 것을 확인할 수 있습니다.

### 4.2 실제 보드 연결 테스트 (D3 또는 AI-G)
1. **Linux**:
   - `sudo cp src/linux/99-topst.rules /etc/udev/rules.d/`
   - `sudo udevadm control --reload-rules && sudo udevadm trigger`
2. **Windows**:
   - Telechips VTC USB 드라이버 설치 확인 (장치 관리자에서 VID `140E` 확인).
3. 보드를 FWDN 모드로 부팅하고 호스트 PC와 USB로 연결.
4. 이미저 실행 $\to$ `.fai` 이미지 선택 $\to$ 감지된 TOPST eMMC 선택 $\to$ Write 진행.

---

## 5. `topst-imager` 최신 리포지토리 머지 완료

`rpi-imager`에서 개발 및 검증된 변경사항을 최신 베이스의 `topst-imager`(`dev/add-topst-fwdn` 브랜치)로 안전하게 포팅 및 머지하였습니다.

### 5.1 최신 코드베이스 구조 차이점 반영
- **QML 싱글톤 아키텍처 (`ImageWriterSingleton`)**:
  - 구버전의 `imageWriter` 직접 접근 대신 최신 QML 구조에 맞추어 `ImageWriterSingleton.setFwdnDevice(...)` 및 `ImageWriterSingleton.formatSize(...)` 호출로 일관성 있게 수정.
- **`WritePath` 열거형 패턴 (`ImageWriter::choosePath`)**:
  - 최신 `imagewriter.cpp`의 아키텍처에 맞추어 `WritePath::FwdnDevice`를 `choosePath()`에 등록하고, `startWrite()`의 디스패치 루틴에 결합.
- **취소 및 리소스 정리 파이프라인 (`cancelWrite`, `~ImageWriter`, `onError`)**:
  - 최신 소스의 `_emitCancelled()` 규칙 및 스레드 라이프사이클 관리(`wait(10000)`, `deleteLater()`, `onError` cancel)를 빠짐없이 구현.
- **CMake 빌드 및 툴 동기화**:
  - 최신 `src/CMakeLists.txt`의 `SOURCES` 및 tools post-build copy 명령어 구성.
  - `src/fwdn/`, `src/tools/` (D3/AI-G 플래시 툴 및 부트 펌웨어), `src/linux/99-topst.rules` 복사 완료.

### 5.2 최종 변경 파일 상태 (`topst-imager`)
```
On branch dev/add-topst-fwdn
Changes not staged for commit:
  modified:   src/CMakeLists.txt
  modified:   src/CommonStrings.qml
  modified:   src/drivelist/drivelist.h
  modified:   src/drivelistitem.cpp
  modified:   src/drivelistitem.h
  modified:   src/drivelistmodel.cpp
  modified:   src/drivelistmodel.h
  modified:   src/drivelistmodelpollthread.cpp
  modified:   src/imagewriter.cpp
  modified:   src/imagewriter.h
  modified:   src/wizard/StorageSelectionStep.qml

Untracked files:
  src/fwdn/
  src/linux/99-topst.rules
  src/tools/
```
