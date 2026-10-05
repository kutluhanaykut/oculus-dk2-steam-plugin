# DK2 SteamVR Driver

**Oculus Rift DK2**'yi Windows 10/11 üzerinde **SteamVR uyumlu bir HMD** olarak kullanmanızı sağlayan açık kaynaklı bir SteamVR sürücüsü. Konum kamerası gerekmez: gözlüğün dahili IMU'su ile yalnızca **yön takibi** (sağa/sola/yukarı/aşağı bakma) yapılır, baş sabit bir yükseklikte duruyormuş gibi raporlanır.

Gözlükte tıklama yapabilmeniz için sürücü ayrıca, bakış yönünüzü izleyen bir **sanal kumanda** kaydeder. Tuşları gamepad'inizden (ya da klavyeden) gelir.

> Bu depo daha önce bir 360° video oynatıcısı da içeriyordu. Proje yalnızca SteamVR sürücüsüne odaklanmak üzere sadeleştirildi; oynatıcı kodu git geçmişinde `b0c7506` commit'inde duruyor.

## Gereksinimler

- Windows 10 veya 11 (x64)
- **Visual Studio 2022 veya 2026** — "Masaüstü geliştirme (C++)" bileşeni, ya da Build Tools
- **CMake 3.24+** (Visual Studio ile birlikte gelir)
- **Git** (bağımlılıklar `FetchContent` ile çekilir)
- Steam + SteamVR
- İlk derleme için internet erişimi

Harici bir SDK indirmeniz gerekmez. OpenVR başlıkları, libusb ve hidapi derleme sırasında otomatik olarak çekilir.

## Derleme

```bash
powershell -ExecutionPolicy Bypass -File scripts\build.ps1
```

Çıktılar:

| Dosya | Açıklama |
|---|---|
| `build\bin\drivers\dk2vr\` | SteamVR'a kopyalanacak sürücü ağacı |
| `build\bin\Release\DK2SteamVRDriverInstaller.exe` | Kurulum + teşhis aracı |
| `build\bin\Release\DK2DisplayReport.exe` | Ekran teşhisi (yönetici gerekmez) |
| `build\bin\Release\DK2VRTests.exe` | Birim testleri (donanım gerektirmez) |

Script CMake generator'ını sabitlemez; kurulu en yeni Visual Studio'yu kullanır. Başka bir sürüm istiyorsanız `-Generator "Visual Studio 17 2022"` geçin.

## Kurulum

**Yöntem 1 — kurulum aracı (önerilen).** Yönetici hakkı ister, sürücüyü kopyalar ve ardından bir dizi kontrol + **ekran raporu** yazdırır:

```bash
.\build\bin\Release\DK2SteamVRDriverInstaller.exe
```

**Yöntem 2 — PowerShell.** Yönetici değilse kendini yükseltir:

```bash
powershell -ExecutionPolicy Bypass -File scripts\install_steamvr_driver.ps1
```

İkisi de sürücüyü `<SteamVR>\drivers\dk2vr\` altına yerleştirir. Sonrasında SteamVR'ı yeniden başlatın.

## Bağlantı ve ilk çalıştırma

1. DK2'yi hem **HDMI** hem **USB** ile bağlayın. USB, IMU için gereklidir; konum kamerası kullanılmaz.
2. Windows ekran ayarlarında DK2'yi **"Bu ekranları genişlet"** olarak ayarlayın. DK2 ayrı bir monitör olarak görünmelidir.
3. SteamVR'ı başlatın. DK2, bağlı bir HMD olarak listelenmelidir.

Sürücü açılışta ne gördüğünü ayrıntılı olarak loglar — Windows'un tanıdığı tüm ekranlar, hangisinin DK2 olduğu, seçilen pencere konumu, göz düzeni ve görüntü dönüşü. Bir şey ters giderse ilk bakılacak yer burasıdır:

```
Steam\logs\vrserver.txt
```

## Kumanda ve kısayollar

SteamVR menüsünde ve oyunlarda işaretçi **baktığınız yeri** gösterir. Tuşlar:

| Gamepad (XInput) | Klavye / fare (Ctrl+Alt ile) | İşlev |
|---|---|---|
| `A` veya sağ tetik | **sol tık** | Tıklama (trigger) |
| `B` | **sağ tık** | Kavrama (grip) |
| `Start` | `M` | Uygulama menüsü |
| `Back` | `H` | Sistem / SteamVR menüsü |
| Sol çubuk veya D-pad | **ok tuşları** — modifier gerekmez | Touchpad ekseni — oyunlarda hareket |
| `Y` veya sol çubuğa bas | **farenin ileri yan tuşu** (veya `U`) | Touchpad tıklama — **ışınlanmayı tetikler** |
| **Sol tetik** | `T` | Touchpad'e **dokunma** — basmadan, kaydırmadan |
| **Sağ çubuk** | **fareyi hareket ettir** | Eli bakıştan bağımsız nişanla |
| **RB / LB** | **fare tekerleği** (veya `PageUp`/`PageDown`) | Eli uzat / geri çek |
| `X` | **tekerleğe tıkla** (veya `C`) | Eli başlangıç konumuna döndür |
| `Back` + `Start` | `R` | Bakışı yeniden merkezle |

Elin bakıştan bağımsız hareket edebilmesi, oyunlarda bir şeye uzanıp tutabilmek için gerekli: sabit bir bakış işaretçisi menüde tıklamaya yeter ama nesne kavramaya yetmez. Nişan bakış yönünden en fazla 60° sapabilir, uzaklık 15 cm ile 90 cm arasında ayarlanır (`dk2vr_input` altından değiştirilebilir). Tekerleğin her tıkı eli 5 cm oynatır (`wheel_step_meters`).

Tekerlek, tuşların aksine bir durum değil bir delta taşıdığı için `GetAsyncKeyState` ile okunamaz; sürücü bunun yerine `RIDEV_INPUTSINK` ile Raw Input dinler. Düşük seviye fare kancası (`WH_MOUSE_LL`) yerine bu tercih edildi: kanca makinedeki her uygulamanın girdi yoluna girer, Raw Input yalnızca dinler. Tekerlek de diğer kısayollar gibi `Ctrl+Alt` ister, yoksa bir web sayfasını kaydırmak eli ileri geri çekerdi.

Klavye yalnızca **gamepad bağlı değilken** devreye girer. Tüm klavye kısayolları `Ctrl+Alt` gerektirir, çünkü sürücü `vrserver.exe` içinde çalışır ve hangi pencerede yazdığınızdan bağımsız olarak tuşları görür — sıradan yazıya tepki vermemesi için.

### Modifier'dan tamamen kurtulmak

`Ctrl+Alt+V` **doğrudan giriş modunu** açıp kapatır. Açıkken tuşların ve fare düğmelerinin hiçbiri `Ctrl+Alt` istemez — iki eliniz de serbest kalır. Sürücü her geçişi loga yazar (`DK2 input: direct mode ON/OFF`), böylece hangi durumda olduğunuzdan emin olabilirsiniz.

Açık bırakmak masaüstünü bozmaz: sürücü girdiyi yalnızca **okur**, tüketmez — tıklamalarınız normal uygulamalara gitmeye devam eder. Yine de VR dışındayken kapatmak, farkında olmadan VR'da bir şeye tıklamanızı önler. Her zaman açık başlamasını isterseniz `direct_input_mode` ayarını `true` yapın.

Sanal kumanda kendini bir **Vive wand** olarak tanıtır. Bunun nedeni pratik: SteamVR ve neredeyse tüm oyunlar bu kumanda tipi için hazır tuş atamalarına sahip, dolayısıyla ek ayar yapmadan çalışır.

### Sahne yanımda veya arkamda açılıyor

Konum kamerası olmadığı için yaw'ın **mutlak referansı yok**: sürücü açılışta gözlüğün hangi yöne baktığını bilemez, oyun da kendi "ileri"sini kendi koordinatında tanımlar. İkisi herhangi bir açıyla ayrışabilir. SteamVR'ın "Reset Seated Position" özelliği de burada yetmez, çünkü kendi oturma sıfır pozunu bizim merkezlememizin üstüne uygular.

Çözüm, ileri yönü **elle bir kez** ayarlamak. Sürücü bunu doğrudan kendi çıktısında uyguladığı için SteamVR'ın ne yaptığı önemsiz kalır:

| Klavye | Gamepad | İşlev |
|---|---|---|
| `Ctrl+Shift+Alt` + `←` / `→` | `Back` + D-pad `←` / `→` | Dünyayı çevir |

Oyunu açın, bakmak istediğiniz yöne dönün, sahne doğru hizalanana kadar çevirin. Bu kadar.

Ayar **kendiliğinden kaydedilir** (`yaw_offset_degrees`), yani bir sonraki açılışta hazır gelir. Ayrıca **merkezleme artık bunu bozmuyor**: `Back`+`Start` sürüklenmeyi temizler ama sizin ayarladığınız ileri yönüne geri döner — sıfıra değil.

Çevirme hızı saniyede 45°; `yaw_adjust_speed_degrees_per_second` ile değiştirilebilir.

### Oyunlarda hareket etme (ışınlanma)

Vive wand kullanan oyunların çoğu — The Lab dahil — ilerlemeyi **touchpad** ile yapar, tetikle değil. Tipik sıra:

Çoğu oyunda (The Lab dahil) belirleyici olan **touchpad'e basmak**, yön ekseni değil:

1. `Y` tuşunu (ya da farenin ileri yan tuşunu) **basılı tutun** — ışınlanma arkı belirir.
2. Arkı **sağ çubukla veya fareyle** nişanlayın.
3. **Bırakın** — oyun sizi oraya taşır.

Ok tuşları / sol çubuk yönü, arkın yönünü ayarlamak isteyen oyunlar için; tek başına ilerlemez.

Bazı oyunlar arkı **basmayla** değil **dokunmayla** çıkarır ve dokunma bırakılınca ışınlar. Onun için ayrı bir binding var: **sol tetik** (gamepad) veya `T`. Basmadan, kaydırmadan sadece "parmağım touchpad'in üzerinde" durumunu üretir — gerçek wand'da ayrı bir sinyaldir ve bazı oyunların beklediği tek şey odur.

Işınlanma tutmuyorsa sırasıyla deneyin: `Y` bas-tut-bırak, sonra sol tetik bas-tut-bırak, sonra ikisini birlikte.

Farenizde yan tuş varsa tek elle de yapabilirsiniz: **geri yan tuşu** touchpad'i ileri iter, **ileri yan tuşu** basar.

Işınlanma olmuyorsa `vrserver.txt`'ye bakın: sürücü artık `trackpad click pressed` ve `trackpad axis now (0, 1)` satırlarını yazıyor. Bu satırlar varsa sürücü doğru şeyi gönderiyor, sorun oyunun beklediği jestte demektir.

Hareket tuşlarının `Ctrl+Alt` istememesinin sebebi basit: modifier'ı tutmak, yön tuşuna basmak ve aynı anda fareyle nişan almak üç el gerektiriyor. İsterseniz `trackpad_requires_modifier` ile eski davranışa dönebilirsiniz.

Oyun "use your wand to move forward" gibi bir uyarı veriyorsa aradığı şey budur; sürücü touchpad'in dört eksenini de, dokunma ve tıklama durumlarını da bildiriyor.

Gamepad varsa çok daha rahat: sol çubuk touchpad eksenlerini, çubuğa basmak da tıklamayı verir.

## Önemli: DK2 ekranına pencere koymayın

Bu kurulumun en kritik çalıştırma koşulu, ve ihlal edildiğinde hiçbir log dosyasına yazılmıyor:

> **DK2'nin ekranında hiçbir pencere bulunmamalı.** Ne oyunun kendi penceresi, ne SteamVR'ın ayna görünümü, ne gezgin, hiçbiri. O ekran yalnızca kompozitörün olmalı.

DK2 genişletilmiş masaüstü ekranı olduğu için SteamVR kompozitörü onu tam ekran olarak sahiplenmek zorunda. Orada başka bir pencere varsa bunu yapamaz ve gözlük **düz koyu kırmızıyla** (#8B0000) dolar — ya da üstteki pencerenin arka planını görürsünüz.

Belirtiler ve neden yanıltıcı olduğu:

- SteamVR durum penceresinde `Headset Error — Make sure your headset's display is in fullscreen mode (-202)` yazar
- O penceredeki **"Enable fullscreen mode"** bağlantısı işe yaramaz, çünkü engel duran pencere hâlâ oradadır
- `vrserver.txt` ve `vrcompositor.txt` **tertemizdir**; kırmızı ve çalışan açılışların logları birbirinin aynısıdır
- Oyun kusursuz çalışıyor görünür: kompozitör istatistiğinde binlerce `presents`, `0 dropped`

Pratikte en sık sebebi **pencereli modda açılan bir oyunun penceresinin DK2 ekranına düşmesi.** Oyunu açın, penceresini bir kez ana monitöre sürükleyin; konumu hatırlanır.

Ekran bir kez kaybedildiğinde kendiliğinden geri alınmaz — engeli kaldırdıktan sonra SteamVR'ı yeniden başlatın.

### Hızlı teşhis

Masaüstünden DK2 ekranının bulunduğu bölgeyi yakalayıp piksel **standart sapmasına** bakın:

| sapma | anlamı |
|---|---|
| ~0 | düz dolgu — kompozitör ekranı alamamış, ya da bir pencere kaplıyor |
| > 25 | gerçek oyun görüntüsü, her şey yolunda |

Ortalama renk de ayırt eder: `R` yüksek, `G` ve `B` sıfıra yakınsa kompozitörün kırmızı hata dolgusudur.

## Sorun giderme

### Önce teşhis aracını çalıştırın

Herhangi bir görüntü sorununda ilk adım:

```bash
.\build\bin\Release\DK2DisplayReport.exe
```

SteamVR'ı başlatmadan, yönetici hakkı istemeden, Windows'un tanıdığı tüm ekranları, hangisinin DK2 olduğunu, hangi modda ve **hangi dönüşle** çalıştığını, sürücünün seçeceği yerleşimi ve denenebilecek dört alternatifi yazdırır.

### Görüntü gözlükte yan dönmüş

En sık karşılaşılan durum. DK2 paneli fiziksel olarak dikey (1080x1920) monte edilmiştir; masaüstünü yatay görüyorsanız bunu **Windows döndürüyor** demektir. Ancak SteamVR ekrana DXGI ile doğrudan bastığı için Windows'un döndürmesini atlar — ve görüntü yan gelir.

Sürücü bunu hesaba katar: teşhis aracının `rotation` satırı 0'dan farklıysa aynı dönüşü kendisi uygular. Tahmin tutmazsa elle geçersiz kılın. `Steam\config\steamvr.vrsettings` dosyasını **SteamVR kapalıyken** açıp `dk2vr_display` bölümüne ekleyin:

```json
"dk2vr_display": {
  "image_rotation": 90
}
```

`90`, `180`, `270` ve `0` değerlerini sırayla deneyin; `-1` otomatiğe döner. SteamVR'ı her değişiklikten sonra yeniden başlatın.

Elle dosya düzenlemek yerine yardımcı script'i kullanabilirsiniz (SteamVR kapalıyken çalıştırın, dosyanın yedeğini alır):

```bash
powershell -ExecutionPolicy Bypass -File scripts\set_rotation.ps1 180
```

`-Show` mevcut değerleri yazdırır, `-SwapEyes` gözleri değiştirir, `-EyeSplit 0|1` göz düzenini zorlar, `-MirrorView` ise gözlükteki görüntüyü monitörde aynalar — gözlüğü takmadan neyin döndüğünü görmek için pratik.

Görüntü doğru yönde ama **gözler ters** geliyorsa (derinlik bozuk, göz kaması) `"swap_eyes": true` ekleyin. Gözler yanlış eksende bölünüyorsa `"eye_split"` değerini `0` (yan yana) veya `1` (üst/alt) yapın.

### SteamVR açılıyor ama gözlükte hiçbir şey yok

Kurulum aracını çalıştırıp **ekran raporuna** bakın. `DK2 HDMI ekrani algilandi` testi başarısızsa Windows DK2'yi masaüstüne bağlı bir ekran olarak görmüyor demektir — HDMI kablosunu ve "Bu ekranları genişlet" ayarını kontrol edin. DK2 uzatılmış ekran olarak görünmüyorsa GPU kontrol panelinden ekranı elle eklemeyi deneyin.

Rapor DK2'yi buluyor ama görüntü yoksa `vrserver.txt` içindeki `DK2: output window ...` satırına bakın; oradaki konum ve boyut, Windows'un DK2 için bildirdiğiyle eşleşmelidir.

Alternatif bir yaklaşım: Windows ekran ayarlarından DK2'nin yönünü, teşhis aracı `rotation 0 degrees` diyene kadar değiştirin. O zaman çerçeve arabelleği panelin ham taramasıyla birebir eşleşir ve belirsizlik kalmaz (masaüstünün DK2'de yan görünmesi önemli değil, zaten oraya bakmayacaksınız).

### Gözlükte tek renk kırmızı ekran

**Önce SteamVR'ın durum penceresine bakın.** Büyük ihtimalle şu yazıyordur:

> **Headset Error** — Make sure your headset's display is in fullscreen mode.
> **Enable fullscreen mode** (-202)

Bu bağlantıya tıklayın, kırmızı anında geçer.

Sebebi: DK2 genişletilmiş masaüstü ekranı olduğu için kompozitörün o ekranda **tam ekrana geçmesi** gerekiyor. Geçemediğinde paneli düz koyu kırmızıyla (#8B0000) dolduruyor. Geçiş bazen kendiliğinden tutuyor, bazen tutmuyor — bu yüzden sorun aralıklı görünüyor ve SteamVR'ı yeniden başlatmak bazen düzeltiyor.

> Bu hata **hiçbir log dosyasına yazılmıyor** — ne `vrserver.txt`'ye ne `vrcompositor.txt`'ye. Kırmızı ve çalışan açılışların kompozitör logları birbirinin aynısı. Uyarı sadece SteamVR durum penceresinde duruyor. Kırmızı ekranı loglardan teşhis etmeye çalışmayın, pencereye bakın.

Tıklamak işe yaramazsa veya pencere görünmüyorsa:

1. SteamVR'ı tamamen kapatıp açın
2. Windows'ta DK2'nin hâlâ genişletilmiş ekran olduğunu doğrulayın (`DK2DisplayReport.exe`)
3. `supersample` değerini kontrol edin — 1.4 üstü ayrı bir kırmızı ekran sebebidir (aşağıya bakın)

### Çözünürlük çarpanından kaynaklanan kırmızı

`supersample` değerini SteamVR kendi render ölçeğiyle bir kez daha çarpar, yani asıl ayrılan doku göründüğünden büyük olur. DK2'de **1.4'ün üstünde** kompozitör sessizce pes ediyor. Ölçtüğümüz eşik: 1.25 ve 1.4 sorunsuz, 1.6 kırmızı.

```bash
powershell -ExecutionPolicy Bypass -File scripts\set_rotation.ps1 auto -Supersample 1.4
```

Sürücü 1.4 üstündeki değerlerde loga uyarı yazar. Netlik için `supersample` yerine SteamVR'ın kendi *Ayarlar → Video → Render Çözünürlüğü* kaydırıcısı tercih edilmeli; onun kendi kırpması bu hatayı önlüyor.

### HMD standby'a giriyor

Logda `entering standby` / `leaving standby` satırları birkaç saniyede bir tekrarlıyorsa SteamVR gözlüğün çıkarıldığını sanıyordur. Sürücü `/proximity` bileşenini bildirdiği için bu olmamalı; oluyorsa logda `failed to create /proximity` satırını arayın.

### "Tracker not connected" / baş hareketi çalışmıyor

DK2'nin USB izleme cihazının (PID `0021`) **WinUSB** sürücüsüne bağlı olması gerekir. Windows'un varsayılan HID sürücüsü bu cihazı açtırmaz.

1. [Zadig](https://zadig.akeo.ie/) indirin ve çalıştırın.
2. *Options → List All Devices* işaretleyin.
3. Listeden **Rift DK2** izleme cihazını seçin ve USB ID'nin `2833 0021` olduğunu doğrulayın.
   > **Uyarı:** `2833 2021` DK2'nin içindeki USB hub'dır. Onun sürücüsünü değiştirmeyin; değiştirirseniz arkasındaki izleme cihazı kaybolur.
4. Hedef sürücü olarak **WinUSB** seçip *Replace Driver*'a basın.
5. SteamVR'ı yeniden başlatın.

Alternatif olarak eski Oculus 0.8 runtime'ı kurmak da cihazı WinUSB'ye bağlar.

Sürücü, tracker bağlanamazsa birkaç saniyede bir yeniden denemeye devam eder; DK2'yi SteamVR çalışırken takıp çıkarabilirsiniz.

### Görüntü bulanık veya kenarlar bozuk

Lens distorsiyonu katsayıları `dk2vr_display` altında ayarlanabilir: `distortion_k0`, `distortion_k1`, `distortion_k2`, `distortion_k3` ve renk sapması için `chromatic_aberration`. Varsayılanlar DK2'nin yayınlanmış değerleridir. Distorsiyonu tamamen kapatmak için `"distortion_enabled": false`.

Netlik için `"supersample": 1.5` deneyebilirsiniz (GPU maliyeti artar).

### Ufuk zamanla yatıyor

Olmaması gerekir: sürücü ivmeölçerden yerçekimini okuyup pitch/roll sürüklenmesini sürekli düzeltir. Düzeltme çok yavaş ya da çok agresif geliyorsa `driver_dk2vr` bölümündeki `"gravity_gain"` değerini değiştirin (varsayılan `1.0`, `0` düzeltmeyi kapatır).

Yaw (sağa/sola) ekseninde yavaş sürüklenme normaldir — manyetometre okunmaz, referans yoktur. `Back`+`Start` ile yeniden merkezleyin.

## Ayarlar

Hepsi `Steam\config\steamvr.vrsettings` içinde. Sürücü açılışta hangi değerleri kullandığını loglar.

### `dk2vr_display`

| Anahtar | Varsayılan | Açıklama |
|---|---|---|
| `image_rotation` | `-1` | Görüntü dönüşü: `-1` otomatik, `0`/`90`/`180`/`270` |
| `eye_split` | `-1` | `-1` otomatik, `0` yan yana, `1` üst/alt |
| `swap_eyes` | `false` | Sol/sağ göz yer değiştirir |
| `supersample` | `1.0` | Göz başına render ölçeği (0.5–4.0) |
| `display_frequency` | `0.0` | Tazeleme hızını geçersiz kıl; `0` = Windows'un bildirdiği |
| `window_x/y/width/height` | `0,0,1920,1080` | Yalnızca DK2 ekranı bulunamazsa kullanılır |
| `distortion_enabled` | `true` | Lens distorsiyon düzeltmesi |
| `distortion_k0..k3` | `1.0, 0.22, 0.24, 0.0` | Radyal distorsiyon katsayıları |
| `chromatic_aberration` | `0.006` | Renk sapması düzeltmesi |
| `screen_width_meters` | `0.12576` | Panel fiziksel genişliği |
| `screen_height_meters` | `0.07074` | Panel fiziksel yüksekliği |
| `lens_separation_meters` | `0.0635` | Lens merkezleri arası mesafe |
| `eye_relief_meters` | `0.041` | Göz–panel mesafesi (FOV'u belirler) |

### `driver_dk2vr`

| Anahtar | Varsayılan | Açıklama |
|---|---|---|
| `gravity_gain` | `1.0` | Pitch/roll sürüklenme düzeltmesi gücü; `0` kapatır |
| `seated_height_meters` | `1.6` | Başın raporlandığı sabit yükseklik |
| `model_number` / `serial_number` | — | SteamVR'da görünen kimlik |

### `dk2vr_input`

| Anahtar | Varsayılan | Açıklama |
|---|---|---|
| `controller_enabled` | `true` | Sanal kumandayı kaydet (kapatılırsa tıklama yapılamaz) |
| `gamepad_enabled` | `true` | XInput gamepad okuması |
| `keyboard_enabled` | `true` | Gamepad yokken klavye yedeği |
| `controller_distance_meters` | `0.35` | İşaretçinin gözden uzaklığı |
| `controller_pitch_offset_degrees` | `0.0` | İşaretçiyi aşağı/yukarı eğ |

## Proje yapısı

```
.
├── CMakeLists.txt          # Bağımlılıklar FetchContent ile
├── core/                   # Donanımdan ve OpenVR'dan bağımsız, test edilebilir mantık
│   ├── display_geometry.*  # DK2 ekranını bulma, yerleşim ve döndürme
│   ├── imu_packet.*        # DK1/DK2 sensör raporu çözücü
│   ├── orientation_filter.*# Gyro entegrasyonu + yerçekimi düzeltmesi
│   └── lens_distortion.*   # Barrel distorsiyon ve renk sapması
├── steamvr_driver/         # OpenVR sürücü DLL'i
│   ├── device_provider.*   # IServerTrackedDeviceProvider, teşhis logu
│   ├── hmd_device_driver.* # HMD + IVRDisplayComponent
│   ├── controller_device_driver.*  # Bakış işaretçisi kumandası
│   ├── input_source.*      # XInput + klavye
│   ├── dk2_imu.*           # libusb/hidapi taşıma katmanı
│   └── driver_settings.*   # vrsettings okuma
├── installer/              # Kurulum + teşhis EXE'si
├── tools/                  # DK2DisplayReport: bağımsız ekran teşhisi
├── tests/                  # Birim testleri (donanımsız çalışır)
└── scripts/                # build, package, install
```

`core/` ayrı tutulmasının nedeni testler: paket çözme, sensör füzyonu, ekran yerleşimi ve distorsiyon matematiği DK2 ya da SteamVR olmadan doğrulanabiliyor.

```bash
.\build\bin\Release\DK2VRTests.exe
```

## Lisans

MIT. Üçüncü parti bileşenler (`OpenVR`, `libusb`, `hidapi`) kendi lisanslarına sahiptir; ayrıntılar `THIRD_PARTY_NOTICES.md` içindedir.
