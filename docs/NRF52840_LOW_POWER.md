# nRF52840 저전력 운용과 검증

이 문서는 V1940 Pro Micro nRF52840(nice!nano V2 호환 clone), Adafruit nRF52 core
1.7.0, S140 SoftDevice 환경의 현재 센서 노드를 설명한다. 수치는 실제 보드 전류 측정
결과가 아니며, 코드 변경만으로 배터리 수명 개선률을 확정하지 않는다.

## 현재 동작과 적용한 최적화

- 환경 측정은 BME688 히터를 끈 forced mode로 2초마다 실행한다.
- HP-354 parallel gas scan은 10분마다 실행하며 프로파일, 140 ms polling, 유효성
  검사를 그대로 유지한다. 히터가 켜지는 이 구간이 평균 전력의 중요한 부분이다.
- BLE NUS의 20-byte chunk/newline 형식과 연결 유지, 10초 STAT, 재연결 광고를
  유지한다.
- `Bluefruit.begin()` 뒤 `sd_power_mode_set(NRF_POWER_MODE_LOWPWR)`를 명시해
  System ON variable-latency low-power 모드를 고정한다. nRF52840은 원래 이 모드가
  기본값이므로, 이는 다른 코드가 Constant Latency를 선택하지 않았음을 명시하고
  진단하는 방어적 설정이다. 그 자체의 절감량은 아직 측정하지 않았다.
- 센서 loop의 유휴 delay를 5 ms에서 10 ms로 늘렸다. 2초 ENV 및 140 ms gas
  polling보다 충분히 짧으며, 불필요한 loop task wakeup을 줄인다. BLE radio와
  callbacks는 SoftDevice/FreeRTOS가 별도로 처리한다.
- 설치된 Adafruit core는 `configUSE_TICKLESS_IDLE=1`이며 idle에서
  `sd_app_evt_wait()`를 사용한다. 따라서 스케치에 별도의 `WFE`/무한 sleep loop를
  추가하지 않는다. 중복 sleep은 BLE와 RTOS scheduling을 방해할 수 있다.

근거: [nRF52840 POWER 사양](https://docs.nordicsemi.com/r/bundle/ps_nrf52840/page/power.html),
[Adafruit nRF52 FreeRTOS 설정](https://github.com/adafruit/Adafruit_nRF52_Arduino/blob/master/cores/nRF5/freertos/config/FreeRTOSConfig.h),
[Adafruit core 저전력 변경 이력](https://github.com/adafruit/Adafruit_nRF52_Arduino/blob/master/changelog.md).

## System ON과 System OFF

System ON에서는 CPU와 주변장치가 RUN/IDLE을 오가며 RAM, RTC, SoftDevice의 BLE
연결 상태를 유지한다. Low-power sub-mode는 wake latency가 가변인 대신 자동 전력
관리를 사용한다. FreeRTOS tickless idle은 다음 작업 시각까지 RTC를 사용하고, BLE
event/interrupt가 있으면 CPU를 깨운다.

System OFF는 core와 대부분의 주변장치를 끄고 진행 중인 BLE 연결과 작업을 끝낸다.
깨울 수 있는 원인은 GPIO/LPCOMP/NFC field/VBUS/reset이며 wake는 reset으로 처리된다.
내부 RTC만으로 2초마다 깨어 ENV를 측정하고 같은 BLE 연결을 유지하는 현재 요구에는
맞지 않는다. 따라서 현재 펌웨어에는 적용하지 않았다. System OFF는 장기 보관 모드,
외부 RTC/alarm 또는 사용자가 누르는 wake pin을 갖춘 별도 실험으로 다룬다.

## BLE와 CPU idle

연결 간격이 길고 slave latency가 크면 radio wakeup 횟수를 줄일 수 있지만, 실제 간격은
central(ESP32)과 협상되며 전송 지연·supervision timeout·재연결에 영향을 준다. 현재
2초 ENV와 DFU/NUS 호환성을 실기 검증하지 않은 상태에서 연결 파라미터를 바꾸지 않았다.
향후에는 현재 negotiated interval/latency를 먼저 기록하고 30/50/100 ms 후보를 같은
RSSI 조건에서 비교한다. Nordic 문서는 100 ms 연결 간격에서 peripheral link 1개와
양방향 packet 1개인 예의 CPU 가용 시간이 99% 이상이라고 설명하지만, 이는 보드 전체
전류나 본 프로젝트의 측정값이 아니다.

근거: [S140 peripheral connection processor usage](https://docs.nordicsemi.com/r/bundle/sds_s140/page/sds/s1xx/ble_processor_avail_interrupt_latency/ble_peripheral_connection_performance.html),
[connection parameter 제약](https://docs.nordicsemi.com/r/bundle/s140_v5.0.0-2.alpha_api/page/structble_gap_conn_params_t.html).

## BME688 히터와 주변장치

Bosch 자료는 BME688 heater 320 °C 연속 운전의 typ. current를 12 mA, 초기 peak를
17 mA로 제시한다. 실제 HP-354 profile의 평균은 profile, 공급 전압, scan 간격에 따라
달라지므로 datasheet 숫자를 현재 평균 전류로 대입하지 않는다. 현재 구현은 ENV 중
heater OFF와 10분 scan을 유지하며 정확도 검증 전 profile을 줄이지 않는다.

OLED, 보드 power/charge LED, regulator quiescent current, pull-up, 외부 센서가 System ON
SoC idle보다 큰 고정 부하가 될 수 있다. 표시를 끄거나 LED를 제거하는 실험은 사용자
동작 변경 및 보드 개조이므로 이번 변경에 포함하지 않았다. USB가 연결되면 USB regulator,
CDC/debug 상태도 battery-only와 다르므로 전력 비교는 USB를 분리해 수행한다.

근거: [BME688 제품/전류 정보](https://www.bosch-sensortec.com/en/products/environmental-sensors/gas-sensors/bme688),
[BME688 datasheet](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bme688-ds000.pdf).

## DC/DC를 자동 활성화하지 않은 이유

nRF52840 DC/DC는 대응하는 외부 LC filter가 실장된 경우 효율을 높일 수 있다. Nordic은
LC filter 없이 DC/DC를 켜면 debug access까지 포함해 동작을 막을 수 있다고 경고한다.
V1940 clone의 정확한 schematic/BOM과 REG0/REG1 inductors가 확인되지 않았으므로
레지스터나 SoftDevice DC/DC 설정을 변경하지 않았다. PCB 실장 확인과 공급 경로 확인을
마친 뒤 별도 펌웨어에서만 시험한다.

근거: [nRF52840 power supply와 DC/DC 경고](https://docs.nordicsemi.com/r/bundle/ps_nrf52840/page/power.html),
[Nordic reference circuitry](https://docs.nordicsemi.com/r/bundle/ps_nrf52840/page/ref_circuitry.html).

## PPK2로 효과 측정하기

Nordic Power Profiler Kit II는 Source Meter 또는 Ampere Meter로 사용한다. V1940의
LiPo 입력 경로가 허용하는 전압을 먼저 확인하고, PPK2 Source Meter 출력과 DUT GND를
연결한다. USB/배터리를 동시에 연결하지 않으며 처음에는 current limit을 보수적으로
설정한다.

1. 기존 펌웨어와 변경 펌웨어를 같은 보드, 공급 전압, RSSI, ESP32 위치에서 비교한다.
2. 최소 20분을 기록해 idle, 2초 ENV, 10분 gas scan을 모두 포함한다.
3. 평균/peak/누적 charge와 BLE reconnect 횟수를 함께 저장한다.
4. OLED/LED를 포함한 실제 사용 상태와 표시를 끈 실험 상태를 별도 기록한다.
5. gas scan이 포함되지 않은 짧은 구간만으로 하루 수명을 외삽하지 않는다.

PPK2는 500 nA–1 A 범위와 최대 100 kS/s를 제공한다. 자세한 연결과 저장 절차는
[PPK2 사용자 안내](https://docs.nordicsemi.com/r/bundle/ug_ppk2/page/ug/ppk/ppk_user_guide_intro.html)와
[Power Profiler app 사용법](https://docs.nordicsemi.com/r/bundle/nrf-connect-for-desktop/page/app/pc-nrfconnect-ppk/using_ppk_app.html)을 따른다.

## 향후 안전한 실험

- BLE negotiated parameters를 먼저 계측한 뒤 연결 간격을 한 변수씩 변경한다.
- OLED refresh와 display sleep의 사용자 경험/전류를 분리 비교한다.
- V1940 schematic/BOM 확인 뒤에만 DC/DC 후보를 시험한다.
- HP-354 profile 변경은 gas fingerprint 재현성과 분류 성능 검증을 별도로 수행한다.
- System OFF는 외부 wake source와 재부팅/재연결 상태 머신이 있는 별도 보관 모드로만
  검토한다.

현재 확정할 수 있는 것은 기존 core가 이미 System ON tickless idle을 사용한다는 점과
새 코드가 Low-power mode를 명시하고 loop wakeup 빈도를 낮췄다는 점이다. 실제 평균
전류, 1000 mAh 배터리 사용 시간, 절감률은 PPK2 또는 동등한 계측 전까지 미검증이다.
