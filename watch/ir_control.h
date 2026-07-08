/**
 * @file      ir_control.h
 * @brief     Controle IR generico (TV + AC) por hierarquia de classes.
 *
 * Construido por partes. Peca 1: comandos + raiz IrDevice.
 *                          Peca 2: familia TV (IrDeviceTV) + encoder NecTV.
 *                          Peca 3: RokuRefTV (mapa IR de referencia Roku).
 *                          Peca 4: marcas Roku (TCL/SEMP/AOC) - concretas.
 *                          Peca 5: LgTV (outra NecTV, mapa LG 0x20DF).
 *                          Peca 6: RcaTV + TclAndroidTV (RCA via sendRaw).
 *                          Peca 7: familia AC (IrDeviceAC) + ElectraAC.
 *                          Peca 8: HAL do Electra (hal_interface.cpp).
 *                          Peca 9: UniversalTVRemote (registry de TVs).
 *                          Peca 10: UniversalACRemote (registry de ACs).
 *                          Peca 11: MideaAC/KomecoMideaAC.
 *                          Peca 12: LG/Samsung/Toshiba/Springer-Midea AC.
 */
#pragma once
#include <stdint.h>

// ---------------------------------------------------------------------------
// Comandos logicos. Separados por familia: TV (stateless/toggle) e AC (stateful).
// ---------------------------------------------------------------------------
/** @brief Logical TV commands (stateless / toggle); each TV maps these to IR codes. */
enum TvCommand : uint8_t {
    TV_POWER,
    TV_VOL_UP,
    TV_VOL_DOWN,
    TV_CH_UP,
    TV_CH_DOWN,
    TV_MUTE,
    TV_INPUT,
    // Navegacao (D-pad) + Home. Codigos NEC capturados: Roku via Flipper-IRDB
    // (TCL_ROKU_US.ir), LG via ESPHome (controle AKB74475481). RCA (TclAndroid)
    // sem bytes de nav -> 0. Validar na TV real (codigos de fonte, nao de chute).
    TV_UP,
    TV_DOWN,
    TV_LEFT,
    TV_RIGHT,
    TV_OK,
    TV_HOME,
};

/** @brief Logical A/C commands (stateful: the device holds temp/mode/fan). */
enum AcCommand : uint8_t {
    AC_POWER,
    AC_TEMP_UP,
    AC_TEMP_DOWN,
    AC_MODE_COOL,
    AC_MODE_HEAT,
    AC_MODE_FAN,
    AC_FAN_UP,
    AC_FAN_DOWN,
    AC_SWING,
};

// ---------------------------------------------------------------------------
// Raiz de todo aparelho IR. So o que TV e AC tem em comum:
//  - um nome (pra UI)
//  - ligar/desligar (pra o "blast" universal e botao de power)
// TV e AC adicionam seus proprios execute(...) tipados nas subclasses.
// ---------------------------------------------------------------------------
/**
 * @brief Root of every IR device. Holds only what TV and A/C share: a display
 *        name and a power toggle. Subclasses add their typed execute(...).
 */
class IrDevice {
public:
    virtual ~IrDevice() = default;

    /** @brief Name shown in the UI (e.g. "TV TCL", "Electrolux AC"). */
    virtual const char *name() const = 0;

    /** @brief Toggle power: a TV sends POWER, an A/C flips state and resends. */
    virtual void powerToggle() = 0;
};

// Declaracoes adiantadas das funcoes do HAL (evita incluir hal_interface.h aqui).
void hw_set_remote_code(uint32_t nec_code);                                // sendNEC
void hw_ir_send_raw(const uint16_t *data, uint16_t len, uint16_t khz);     // sendRaw
// Envia o estado de um AC Electra/Electrolux (HAL embrulha a classe da lib).
void hw_ac_electra_send(bool power, uint8_t temp, uint8_t mode, uint8_t fan);
// Envia estado Midea-compatible; swing_toggle e' comando momentaneo.
void hw_ac_midea_send(bool power, uint8_t temp, uint8_t mode, uint8_t fan, bool swing_toggle);
void hw_ac_coolix_send(bool power, uint8_t temp, uint8_t mode, uint8_t fan, bool swing_toggle);
void hw_ac_lg_send(bool power, uint8_t temp, uint8_t mode, uint8_t fan, bool swing, bool swing_toggle);
void hw_ac_samsung_send(bool power, uint8_t temp, uint8_t mode, uint8_t fan, bool swing);
void hw_ac_toshiba_send(bool power, uint8_t temp, uint8_t mode, uint8_t fan, bool swing, bool swing_toggle);

// "Super Turn On": brute-force power-on across common Brazilian AC protocols.
// count() = number of protocols, name(i) = its label, send_on(i) = fire its
// power-on now. The UI steps i=0..count-1 to find which one drives the unit.
uint8_t hw_ac_blast_count();
const char *hw_ac_blast_name(uint8_t i);
void hw_ac_blast_send_on(uint8_t i);

// ---------------------------------------------------------------------------
// Familia TV: define o execute(TvCommand) e ja resolve o powerToggle.
// Ainda abstrata (execute e' puro -> cada TV implementa).
// ---------------------------------------------------------------------------
class IrDeviceTV : public IrDevice {
public:
    // Executa um comando de TV. Cada TV concreta implementa.
    virtual void execute(TvCommand cmd) = 0;

    // powerToggle da raiz, resolvido aqui: mandar o comando POWER.
    void powerToggle() override { execute(TV_POWER); }
};

// ---------------------------------------------------------------------------
// Encoder NEC generico: implementa execute mandando o codigo NEC do comando.
// A subclasse so fornece o mapa codeFor(cmd) (o hex de cada comando).
// Ainda abstrata (codeFor e' puro).
// ---------------------------------------------------------------------------
class NecTV : public IrDeviceTV {
public:
    void execute(TvCommand cmd) override {
        uint32_t code = codeFor(cmd);
        if (code != 0) {            // 0 = comando nao suportado por essa TV
            hw_set_remote_code(code);  // sendNEC no HAL
        }
    }

protected:
    // Hex NEC (wire-order, pronto pro sendNEC) de cada comando. 0 = sem suporte.
    virtual uint32_t codeFor(TvCommand cmd) const = 0;
};

// ---------------------------------------------------------------------------
// Mapa IR de referencia Roku (NEC, prefix 0x57E3). Codigos validados (JP1/IRDB).
// NAO e' marca -> e' o protocolo compartilhado. Ainda abstrata (name e' puro):
// as marcas (TclRoku/SEMP/AOC Roku) herdam e so dao o nome.
// ---------------------------------------------------------------------------
class RokuRefTV : public NecTV {
protected:
    uint32_t codeFor(TvCommand cmd) const override {
        switch (cmd) {
            case TV_POWER:    return 0x57E3E817;  // toggle
            case TV_VOL_UP:   return 0x57E3F00F;
            case TV_VOL_DOWN: return 0x57E308F7;
            case TV_CH_UP:    return 0x57E3D827;
            case TV_CH_DOWN:  return 0x57E338C7;
            case TV_MUTE:     return 0x57E304FB;
            case TV_INPUT:    return 0x57E3748B;
            // D-pad + Home: capturado do Flipper-IRDB (TCL_ROKU_US.ir), addr EA C7
            // = wire 0x57E3, comando = bit-reverse de cada byte do Flipper.
            case TV_UP:       return 0x57E39867;
            case TV_DOWN:     return 0x57E3CC33;
            case TV_LEFT:     return 0x57E37887;
            case TV_RIGHT:    return 0x57E3B44B;
            case TV_OK:       return 0x57E354AB;  // Select
            case TV_HOME:     return 0x57E3C03F;
        }
        return 0;  // comando desconhecido
    }
};

// ---------------------------------------------------------------------------
// Marcas Roku: concretas (so dao o nome). Herdam codigos + execute do RokuRefTV.
// Hoje identicas; se uma marca divergir num codigo, basta dar override de
// codeFor() so nela (sem afetar as outras).
// ---------------------------------------------------------------------------
class TclRokuTV : public RokuRefTV {
public:
    const char *name() const override { return "TV TCL (Roku)"; }
};

class SempRokuTV : public RokuRefTV {
public:
    const char *name() const override { return "TV SEMP (Roku)"; }
};

class AocRokuTV : public RokuRefTV {
public:
    const char *name() const override { return "TV AOC (Roku)"; }
};

// ---------------------------------------------------------------------------
// LG: outra NecTV (protocolo NEC proprio, prefix 0x20DF, validado IRDB/LIRC).
// LG nao tem variantes de marca -> uma classe so, concreta (name + codeFor).
// (Diferente do Roku, que separou ref + marcas.)
// ---------------------------------------------------------------------------
class LgTV : public NecTV {
public:
    const char *name() const override { return "TV LG"; }

protected:
    uint32_t codeFor(TvCommand cmd) const override {
        switch (cmd) {
            case TV_POWER:    return 0x20DF10EF;  // toggle
            case TV_VOL_UP:   return 0x20DF40BF;
            case TV_VOL_DOWN: return 0x20DFC03F;
            case TV_CH_UP:    return 0x20DF00FF;
            case TV_CH_DOWN:  return 0x20DF807F;
            case TV_MUTE:     return 0x20DF906F;
            case TV_INPUT:    return 0x20DFD02F;
            // D-pad + Home (LG NEC 0x20DF, IRDB/LIRC). Testar na TV.
            case TV_UP:       return 0x20DF02FD;
            case TV_DOWN:     return 0x20DF827D;
            case TV_LEFT:     return 0x20DFE01F;
            case TV_RIGHT:    return 0x20DF609F;
            case TV_OK:       return 0x20DF22DD;  // Enter
            case TV_HOME:     return 0x20DF3EC1;  // Home/Smart
        }
        return 0;
    }
};

// ---------------------------------------------------------------------------
// RCA: ramo IRMAO do NecTV (protocolo RCA, NAO esta na IRremoteESP8266).
// Monta o frame de 24 bits e manda por hw_ir_send_raw (sendRaw).
// A subclasse so fornece cmdByte(cmd) (o byte de comando RCA).
//
// Frame RCA: header + [addr(4) cmd(8)] + complemento(12), MSB-first.
// AVISO: portadora RCA classica e' ~56 kHz; uso 38 kHz aqui (variante "RCA-38").
// Timings/freq nao confirmados em hardware -> testar na TV real.
// ---------------------------------------------------------------------------
class RcaTV : public IrDeviceTV {
public:
    void execute(TvCommand cmd) override {
        uint8_t cb = cmdByte(cmd);
        if (cb == 0) return;             // 0 = comando nao suportado
        sendRca(deviceAddr(), cb);
    }

protected:
    virtual uint8_t cmdByte(TvCommand cmd) const = 0;  // byte de comando RCA
    virtual uint8_t deviceAddr() const { return 0x0F; } // device 15 (default)

private:
    void sendRca(uint8_t addr, uint8_t cmd) {
        const uint16_t HDR_MARK = 4000, HDR_SPACE = 4000;
        const uint16_t BIT_MARK = 500, ONE_SPACE = 2000, ZERO_SPACE = 1000;
        const uint16_t GAP = 8000;

        // 12 bits logicos + 12 bits complementados = 24 bits.
        uint16_t payload = (uint16_t)((addr & 0x0F) << 8) | cmd;
        uint32_t frame = ((uint32_t)payload << 12) | (~payload & 0x0FFF);

        uint16_t buf[2 + 24 * 2 + 2];
        uint16_t n = 0;
        buf[n++] = HDR_MARK;
        buf[n++] = HDR_SPACE;
        for (int i = 23; i >= 0; --i) {        // MSB-first
            buf[n++] = BIT_MARK;
            buf[n++] = (frame & (1UL << i)) ? ONE_SPACE : ZERO_SPACE;
        }
        buf[n++] = BIT_MARK;
        buf[n++] = GAP;
        hw_ir_send_raw(buf, n, 38);
    }
};

// TCL Android/Google TV: concreta. RCA, device 15. Bytes de comando capturados
// do Flipper-IRDB (TVs/TCL/TCL_43S446.ir e TCL_65C635K.ir, protocol RCA addr 0F).
// Google TV nao tem canal -> CH = 0. TV_INPUT usa o botao "Tv"/source (0x58).
class TclAndroidTV : public RcaTV {
public:
    const char *name() const override { return "TV TCL (Android)"; }

protected:
    uint8_t cmdByte(TvCommand cmd) const override {
        switch (cmd) {
            case TV_POWER:    return 0x54;
            case TV_VOL_UP:   return 0xF4;
            case TV_VOL_DOWN: return 0x74;
            case TV_MUTE:     return 0xFC;
            case TV_INPUT:    return 0x58;   // botao "Tv" (source)
            case TV_UP:       return 0x9A;
            case TV_DOWN:     return 0x1A;
            case TV_LEFT:     return 0x6A;
            case TV_RIGHT:    return 0xEA;
            case TV_OK:       return 0x2F;   // Select
            case TV_HOME:     return 0x10;
            case TV_CH_UP: case TV_CH_DOWN: return 0;  // Google TV sem canal
        }
        return 0;
    }
};

// ===========================================================================
// LADO AC
// ===========================================================================

// Modos e fan genericos (cada vendor mapeia pros seus valores no HAL).
/** @brief Generic A/C operating mode (vendor HAL maps it to device values). */
enum AcMode : uint8_t { ACM_COOL, ACM_HEAT, ACM_FAN };
/** @brief Generic A/C fan speed (vendor HAL maps it to device values). */
enum AcFan : uint8_t  { ACF_LOW, ACF_MID, ACF_HIGH, ACF_AUTO };

// ---------------------------------------------------------------------------
// Familia AC: STATEFUL. Guarda estado (power/temp/modo/fan) e a logica
// comando->estado e' COMPARTILHADA aqui. So o ENCODING (mandar o estado) muda
// por vendor -> sendState() e' puro. Ainda abstrata (name + sendState puros).
//
// (Contraste com TV: la o que variava era o dado/codeFor; aqui a logica de
//  comando e' comum e o que varia e' como serializar o estado.)
// ---------------------------------------------------------------------------
class IrDeviceAC : public IrDevice {
public:
    void execute(AcCommand cmd) {
        swing_toggle_pending = false;
        switch (cmd) {
            case AC_POWER:     power = !power;             break;
            case AC_TEMP_UP:   if (temp < 30) temp++;     break;
            case AC_TEMP_DOWN: if (temp > 16) temp--;     break;
            case AC_MODE_COOL: mode = ACM_COOL;           break;
            case AC_MODE_HEAT: mode = ACM_HEAT;           break;
            case AC_MODE_FAN:  mode = ACM_FAN;            break;
            case AC_FAN_UP:    if (fan < ACF_AUTO) fan++; break;
            case AC_FAN_DOWN:  if (fan > ACF_LOW)  fan--; break;
            case AC_SWING:     swing = !swing; swing_toggle_pending = true; break;
        }
        sendState();   // AC manda o estado COMPLETO a cada comando
        swing_toggle_pending = false;
    }

    void powerToggle() override { power = !power; sendState(); }

protected:
    // Estado atual do aparelho (AC e' stateful).
    bool    power = false;
    uint8_t temp  = 24;
    uint8_t mode  = ACM_COOL;
    uint8_t fan   = ACF_AUTO;
    bool    swing = false;
    bool    swing_toggle_pending = false;

    // Serializa+manda o estado. Cada vendor implementa (lib propria / raw).
    virtual void sendState() = 0;
};

// ---------------------------------------------------------------------------
// Electra/Electrolux: usa a classe IRElectraAc da lib, via HAL (mantem este
// header portavel, sem incluir a lib). O HAL traduz estado -> setTemp/setMode/send.
// ---------------------------------------------------------------------------
class ElectraAC : public IrDeviceAC {
public:
    const char *name() const override { return "AC Electrolux (Electra)"; }

protected:
    void sendState() override {
        hw_ac_electra_send(power, temp, mode, fan);
    }
};

// ---------------------------------------------------------------------------
// Midea-compatible: cobre Midea e deve ser o primeiro teste para Komeco/Comfee
// quando o controle fisico e' da familia R05/R06/R71. Ainda precisa teste real.
// ---------------------------------------------------------------------------
class MideaAC : public IrDeviceAC {
public:
    const char *name() const override { return "AC Midea"; }

protected:
    void sendState() override {
        hw_ac_midea_send(power, temp, mode, fan, swing_toggle_pending);
    }
};

// Coolix: familia real de muitos splits BR vendidos como Midea/Springer/Komeco/
// Elgin. Confirmado em hardware que este aparelho responde a COOLIX (nao MIDEA).
class CoolixAC : public IrDeviceAC {
public:
    const char *name() const override { return "AC Coolix (Midea/Elgin)"; }

protected:
    void sendState() override {
        hw_ac_coolix_send(power, temp, mode, fan, swing_toggle_pending);
    }
};

// Komeco: controles de reposicao BR sao vendidos como Midea/Komeco/Comfee
// (ref. R05/R06, ex. R06/BGCE) -> familia Midea. Por isso herda de MideaAC.
class KomecoMideaAC : public MideaAC {
public:
    const char *name() const override { return "AC Komeco (Midea)"; }
};

class SpringerMideaAC : public MideaAC {
public:
    const char *name() const override { return "AC Springer (Midea)"; }
};

class LgAC : public IrDeviceAC {
public:
    const char *name() const override { return "AC LG"; }

protected:
    void sendState() override {
        hw_ac_lg_send(power, temp, mode, fan, swing, swing_toggle_pending);
    }
};

class SamsungAC : public IrDeviceAC {
public:
    const char *name() const override { return "AC Samsung"; }

protected:
    void sendState() override {
        hw_ac_samsung_send(power, temp, mode, fan, swing);
    }
};

class ToshibaAC : public IrDeviceAC {
public:
    const char *name() const override { return "AC Toshiba"; }

protected:
    void sendState() override {
        hw_ac_toshiba_send(power, temp, mode, fan, swing, swing_toggle_pending);
    }
};

// ---------------------------------------------------------------------------
// Controle universal de TV: lista TVs diferentes pela base IrDeviceTV.
// Nao mistura com AC, porque TV usa TvCommand e AC usa AcCommand.
// Nao e' dono dos objetos; so guarda enderecos de TVs criadas fora.
// ---------------------------------------------------------------------------
class UniversalTVRemote {
public:
    static const uint8_t MAX_DEVICES = 12;

    bool add(IrDeviceTV *dev) {
        if (dev == nullptr || count >= MAX_DEVICES) return false;
        devices[count++] = dev;
        return true;
    }

    IrDeviceTV *device(uint8_t index) const {
        if (index >= count) return nullptr;
        return devices[index];
    }

    uint8_t size() const { return count; }

    void execute(uint8_t index, TvCommand cmd) {
        IrDeviceTV *dev = device(index);
        if (dev != nullptr) dev->execute(cmd);
    }

    void blastPower() {
        for (uint8_t i = 0; i < count; ++i) {
            devices[i]->powerToggle();
        }
    }

private:
    IrDeviceTV *devices[MAX_DEVICES] = {};
    uint8_t count = 0;
};

// ---------------------------------------------------------------------------
// Controle universal de AC: lista ACs diferentes pela base IrDeviceAC.
// Separado do controle de TV porque comandos e estado sao diferentes.
// Nao e' dono dos objetos; so guarda enderecos de ACs criados fora.
// ---------------------------------------------------------------------------
class UniversalACRemote {
public:
    static const uint8_t MAX_DEVICES = 8;

    bool add(IrDeviceAC *dev) {
        if (dev == nullptr || count >= MAX_DEVICES) return false;
        devices[count++] = dev;
        return true;
    }

    IrDeviceAC *device(uint8_t index) const {
        if (index >= count) return nullptr;
        return devices[index];
    }

    uint8_t size() const { return count; }

    void execute(uint8_t index, AcCommand cmd) {
        IrDeviceAC *dev = device(index);
        if (dev != nullptr) dev->execute(cmd);
    }

    void blastPower() {
        for (uint8_t i = 0; i < count; ++i) {
            devices[i]->powerToggle();
        }
    }

private:
    IrDeviceAC *devices[MAX_DEVICES] = {};
    uint8_t count = 0;
};
