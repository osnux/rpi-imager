# TOPST SBC eMMC 플래싱 지원을 위한 rpi-imager 확장 구현 계획

## 1. 개요 및 분석 결과

### 1.1 배경 및 목적
- **기존 rpi-imager의 동작 방식**:
  - 기본적으로 SD 카드, USB 드라이브와 같은 블록 스토리지 디바이스(raw block device)에 `.iso`, `.img`, 압축 이미지(`.xz`, `.zip` 등)를 direct raw write하는 구조입니다.
  - 최신 rpi-imager 버전은 RPi Compute Module 등을 위해 `rpiboot`(USB 부트스트랩) 및 `FastbootFlashThread`(Android Fastboot 프로토콜을 이용한 eMMC/NVMe 플래싱) 파이프라인을 추가로 갖추고 있습니다.
- **TOPST SBC의 특성 및 과제**:
  - TOPST Single Board Computer(Telechips SoC 탑재, 예: TOPST D3 / TOPST AI-G)는 보드에 **내장 eMMC**를 탑재하고 있습니다.
  - USB로 PC와 연결 시 일반 Mass Storage(블록 장치)로 인식되지 않고, Telechips 독자 프로토콜인 **FWDN (Firmware Download) 모드** 또는 **가상 COM/시리얼 포트**로 인식됩니다.
  - 플래싱 대상 이미지는 단순 raw img/iso가 아니라, Telechips 전용 GPT 파티션 결합 포맷인 **`.fai` (Firmware Artifact Image)** 또는 부트 펌웨어가 포함된 패키지입니다.

---

### 1.2 `topst-development/tools` 분석 결과
[topst-development/tools](https://github.com/topst-development/tools) 레포지토리를 클론하여 내부 도구 및 스크립트를 분석한 결과:

1. **도구 구성**:
   - **`fwdn_d3/`** (TOPST D3 / Telechips TCC8050 등 Dolphin 3 계열):
     - 실행 파일: `fwdn` (Linux 64-bit ELF), `fwdn.exe` (Windows PE), `VtcUsbPort.dll` (Windows 통신 드라이버 DLL)
     - 부트 펌웨어: `boot-firmware/fwdn.json` (`hsm.cs.bin`, `fwdn.rom`, `dram_params.bin`, `mcert.bin`), `boot-firmware/boot.<single|dual>.json`
     - 다운로드 파이프라인 (CLI 흐름):
       1. 부트스트랩(RAM 로딩): `fwdn --fwdn boot-firmware/fwdn.json`
       2. eMMC 포맷: `fwdn --storage emmc --low-format`
       3. 부트로더 기록: `fwdn -w boot-firmware/boot.<mode>.json`
       4. OS FAI 플래싱: `fwdn -w <image>.fai --storage emmc --area user`
   - **`fwdn_ai/`** (TOPST AI-G / Telechips TCC750x 계열):
     - 실행 파일: `fwdn_ai` (Linux), `fwdn_ai.exe` (Windows)
     - 부트 펌웨어: `boot-firmware/tcc750x_fwdn.json`, `boot-firmware/tcc750x_boot.json`
     - 통신 방식: USB 시리얼(COM 포트) 기반 인식, 다운로드 시 `--port <port_num>` 인자 필수.
   - **`mktcimg` / `stitch-fai-*.sh`**:
     - 개별 파티션 파일(bl3 rom, boot.img, dtb, rootfs ext4)을 Telechips GPT 명세(`partition.list.*`)에 맞추어 하나의 통합 `.fai` 파일로 패키징하는 툴.
     - 최종 배포본은 `.fai` 파일이거나 FWDN 펌웨어가 포함된 `.fwdn.zip` 형태.

2. **USB 디바이스 식별 정보**:
   - Vendor ID (VID): `0x140E` (Telechips Inc.)
   - Product ID (PID): SoC 모델에 따라 상이
   - TOPST AI-G의 경우 USB Serial (COM 포트 / CDC ACM) 인터페이스로도 노출됨.

---

## 2. rpi-imager 아키텍처와의 통합 전략

rpi-imager에 이미 구현되어 있는 `rpiboot` 및 `FastbootFlashThread` 패턴을 벤치마킹하여, **`FwdnScanner`**와 **`FwdnFlashThread`**를 구축하는 것이 가장 깔끔하고 확장성 있는 구조입니다.

```
+-----------------------------------------------------------------------------------+
|                                 QML User Interface                                |
|  [Device Step: TOPST D3 / AI] -> [OS Step: .fai / Web Feed] -> [Storage: TOPST eMMC]|
+------------------------------------------+----------------------------------------+
                                           |
                                           v
+-----------------------------------------------------------------------------------+
|                                    ImageWriter                                    |
|   - isTopstDevice() / isFwdnDevice()                                              |
|   - setFwdnDevice(deviceId, boardType, port)                                      |
+------------------------------------------+----------------------------------------+
              |                                            |
              v (Device Detection)                         v (Flashing Engine)
+----------------------------------+       +----------------------------------------+
|           FwdnScanner            |       |            FwdnFlashThread             |
| - USB 감지 (VID 0x140E)          |       | - QProcess 기반 FWDN CLI 래퍼         |
| - COM/Serial 포트 감지           |       | - 1단계: FWDN Bootstrap RAM 로딩       |
| - DriveListModel에 가상 디바이스 |       | - 2단계: eMMC Low-format               |
|   "TOPST Board (eMMC via FWDN)"  |       | - 3단계: Boot Firmware Flashing        |
|   항목 주입                      |       | - 4단계: FAI OS Image Flashing         |
|                                  |       | - stdout 파싱 -> writeProgress emit    |
+----------------------------------+       +----------------------------------------+
```

---

## User Review Required

> [!IMPORTANT]
> **FWDN 바이너리 라이선스 및 번들링 방식 확인**
> `fwdn`, `fwdn_ai`, `VtcUsbPort.dll` 등은 텔레칩스 독점 바이너리로 소스코드가 제공되지 않습니다.
> 따라서 rpi-imager 내부에서 C++로 FWDN USB 저수준 프로토콜을 재구현하는 것보다는, **검증된 CLI 바이너리를 애플리케이션의 리소스(`tools/`)로 번들링하여 `QProcess` 파이프라인으로 호출/제어하는 방식(Subprocess Wrapper)**을 권장합니다.
> 이 방식에 동의하시는지 확인이 필요합니다.

> [!NOTE]
> **보드 지원 범위 및 식별 방식**
> TOPST D3(TCC8050)와 TOPST AI-G(TCC750x) 중 현재 집중하고자 하는 모델이 특정 모델인지, 아니면 두 모델을 모두 지원할지 여부.
> (본 계획안은 D3 및 AI-G 두 모델을 모두 지원하는 범용 설계를 기준으로 작성되었습니다.)

---

## Proposed Changes

### 1. 디바이스 감지 및 스토리지 모델 확장 (Device Discovery)

#### [NEW] [fwdn_scanner.h](file:///c:/Users/wjmoh-home/Documents/work/rpi-imager/src/fwdn/fwdn_scanner.h)
#### [NEW] [fwdn_scanner.cpp](file:///c:/Users/wjmoh-home/Documents/work/rpi-imager/src/fwdn/fwdn_scanner.cpp)
- **역할**: 백그라운드에서 USB 버스를 스캔하여 Telechips FWDN 모드로 연결된 TOPST 보드를 탐색.
- **감지 로직**:
  - VID `0x140E`를 가진 USB 장치 감지 (libusb 또는 Windows SetupAPI).
  - Windows의 경우 `Win32_PnPEntity` / COM 포트 디바이스 목록 스캔 (AI-G 대응).
  - Linux의 경우 `/sys/bus/usb/devices` 또는 udev/libusb 및 `/dev/ttyUSB*` / `/dev/ttyACM*` 스캔.
- **출력**: `Drivelist::DeviceDescriptor` 생성:
  - `device`: `fwdn://topst-d3` 또는 `fwdn://topst-ai?port=COM3`
  - `description`: `"TOPST D3 (eMMC via FWDN)"` 또는 `"TOPST AI-G (eMMC via FWDN)"`
  - `isFwdn`: `true`
  - `isRemovable`: `true`, `size`: 0 (또는 eMMC 기본 크기)

#### [MODIFY] [drivelistmodel.cpp](file:///c:/Users/wjmoh-home/Documents/work/rpi-imager/src/drivelistmodel.cpp)
#### [MODIFY] [drivelistmodel.h](file:///c:/Users/wjmoh-home/Documents/work/rpi-imager/src/drivelistmodel.h)
#### [MODIFY] [drivelistitem.h](file:///c:/Users/wjmoh-home/Documents/work/rpi-imager/src/drivelistitem.h)
- `Drivelist::DeviceDescriptor` 및 `DriveListItem`에 `isFwdn` 프로퍼티 추가.
- 드라이브 폴링 시 `scanFwdnDevices()` 결과를 병합하여 QML에 노출.

---

### 2. TOPST 플래시 실행 스레드 (Flashing Engine)

#### [NEW] [fwdnflashthread.h](file:///c:/Users/wjmoh-home/Documents/work/rpi-imager/src/fwdn/fwdnflashthread.h)
#### [NEW] [fwdnflashthread.cpp](file:///c:/Users/wjmoh-home/Documents/work/rpi-imager/src/fwdn/fwdnflashthread.cpp)
- **역할**: `QThread` 상에서 FWDN CLI 바이너리를 단계별로 실행하고 상태 및 진행률을 UI로 피드백.
- **실행 단계**:
  1. **준비 단계**: 대상 보드(`topst-d3` vs `topst-ai`)에 맞는 FWDN 실행 바이너리 및 `boot-firmware` 경로 해석.
  2. **1단계 (Bootstrap)**: `fwdn --fwdn boot-firmware/fwdn.json` 실행. 보드의 RAM으로 FWDN ROM 업로드.
  3. **2단계 (Low Format)**: `fwdn --storage emmc --low-format` 실행. eMMC 초기화.
  4. **3단계 (Boot Firmware)**: `fwdn -w boot-firmware/boot.json` 실행. 부트로더 작성.
  5. **4단계 (OS Image)**: `fwdn -w <image.fai> --storage emmc --area user` 실행. OS FAI 이미지 플래싱.
- **진행률 및 상태 스트리밍**:
  - `QProcess::readyReadStandardOutput` / `readyReadStandardError` 연결.
  - FWDN 출력 문자열 파싱 (예: 속도, 진행 퍼센티지, 현재 파티션 상태 등) -> `writeProgress(now, total)`, `preparationStatusUpdate(msg)` 시그널 emit.
  - 오류 발생 시 `error(msg)` 시그널 및 프로세스 강제 정리.
  - 취소 시 `cancel()` 슬롯으로 안전하게 자식 프로세스 kill 및 정리.

---

### 3. ImageWriter 코어 통합

#### [MODIFY] [imagewriter.h](file:///c:/Users/wjmoh-home/Documents/work/rpi-imager/src/imagewriter.h)
#### [MODIFY] [imagewriter.cpp](file:///c:/Users/wjmoh-home/Documents/work/rpi-imager/src/imagewriter.cpp)
- 멤버 변수 추가: `bool _isFwdnDevice`, `QString _fwdnBoardType`, `QString _fwdnPort`, `FwdnFlashThread *_fwdnFlashThread`.
- Q_INVOKABLE 메서드:
  - `void setFwdnDevice(const QString &devicePath, const QString &boardType, const QString &port);`
  - `bool isFwdnDevice() const;`
- `startWrite()` 로직 수정:
  - 타겟이 FWDN 디바이스인 경우 일반 블록 디바이스 라이터(`DownloadExtractThread`) 대신 `FwdnFlashThread` 인스턴스를 생성하여 실행.
  - 이미지가 원격 URL인 경우: 다운로드 완료 후 로컬 캐시된 `.fai` 경로를 `FwdnFlashThread`에 전달.
  - 로컬 파일인 경우: 선택된 `.fai` 경로를 직접 전달.

---

### 4. UI / QML 계층 업데이트

#### [MODIFY] [CommonStrings.qml](file:///c:/Users/wjmoh-home/Documents/work/rpi-imager/src/CommonStrings.qml)
- `imageExtensions` 목록에 `"*.fai"` 추가:
  ```qml
  readonly property var imageExtensions: ["*.fai","*.img","*.zip","*.iso","*.gz","*.xz","*.zst","*.wic"]
  ```

#### [MODIFY] [StorageSelectionStep.qml](file:///c:/Users/wjmoh-home/Documents/work/rpi-imager/src/wizard/StorageSelectionStep.qml)
- FWDN 디바이스 전용 아이콘 및 뱃지 표시:
  - `dstitem.isFwdn ? "TOPST Board (eMMC)" : ...`
  - 클릭 시 `imageWriter.setFwdnDevice(...)` 호출 연동.

#### [MODIFY] [config.h](file:///c:/Users/wjmoh-home/Documents/work/rpi-imager/src/config.h) / Manifest
- 필요 시 TOPST 전용 기본 OS 목록 피드 URL 또는 로컬 매니페스트 지원 추가.
- `devices` 목록에 TOPST 보드 정의 (D3-G, AI-G).

---

### 5. 빌드 시스템 및 번들링 구성

#### [MODIFY] [CMakeLists.txt](file:///c:/Users/wjmoh-home/Documents/work/rpi-imager/src/CMakeLists.txt)
- `fwdn_scanner.cpp`, `fwdnflashthread.cpp` 빌드 타겟 추가.
- 빌드 산출물 배포 디렉토리(또는 리소스)에 `topst-tools` 바이너리 디렉토리(`tools/fwdn_d3`, `tools/fwdn_ai`) 복사 설정.
- Linux udev 규칙 (`linux/99-topst.rules`) 추가:
  - `SUBSYSTEM=="usb", ATTR{idVendor}=="140e", MODE="0666"`
- Windows 배포 시 `VtcUsbPort.dll` 포함.

---

## Verification Plan

### 1. 도구 연동 및 CLI 래퍼 검증 (단위 테스트)
- scratch 디렉토리의 `fwdn.exe`, `fwdn_ai.exe`를 사용하여 더미/테스트 파라미터로 실행 테스트:
  - 명령행 인자 전달 및 프로세스 생성/종료 확인
  - stdout/stderr 파싱 정규표현식 검증 (진행률 및 에러 메시지 추출 테스트)

### 2. UI 및 감지 로직 검증
- TOPST 보드를 USB로 연결했을 때(또는 VID `140e` 모의 장치 연결 시) StorageSelectionStep 화면에 "TOPST Board (eMMC via FWDN)" 항목이 올바르게 나타나는지 확인.
- 로컬 파일 브라우저에서 `.fai` 파일이 정상적으로 선택되는지 확인.

### 3. 실제 보드 Flashing End-to-End 검증 (보드 보유 시)
1. TOPST 보드를 FWDN 모드로 진입시키고 호스트 PC에 연결.
2. 커스텀 이미저 실행.
3. OS 선택: TOPST `.fai` 이미지 선택.
4. 스토리지 선택: 감지된 "TOPST eMMC (FWDN)" 선택.
5. "Write" 실행:
   - 1단계(RAM Bootstrap) -> 2단계(Low Format) -> 3단계(Boot FW) -> 4단계(FAI Flash) 진행률 표시 확인.
6. 완료 후 보드 재부팅 및 eMMC 정상 부팅 확인.
