#include "PacketReceiveProcessor.h"

void PacketReceiveProcessor::ProcessReply(const uint8_t* receivebuffer) {

  //Copy to our buffer (probably a better way to share memory than this)
  memcpy(&_packetbuffer, receivebuffer, sizeof(_packetbuffer));

  //Calculate the CRC and compare to received
  uint16_t validateCRC = uCRC16Lib::calculate((char*)&_packetbuffer, sizeof(_packetbuffer) - 2);

  //dumpPacketToDebug(&_packetbuffer);
  //Serial1.print('=');

  if (validateCRC==_packetbuffer.crc) {
      //Serial1.print("good");

      //Subtract good packets from total sent count
      //totalMissedPacketCount should be near zero as possible
      totalMissedPacketCount--;

      if (ReplyWasProcessedByAModule()) {

      switch (ReplyForCommand()) {
        case COMMAND::SetBankIdentity: break;  //Ignore reply
        case COMMAND::ReadVoltageAndStatus: ProcessReplyVoltage();          break;
        case COMMAND::Identify: break;  //Ignore reply
        case COMMAND::ReadTemperature: ProcessReplyTemperature();          break;
        case COMMAND::ReadBadPacketCounter: break;
        case COMMAND::ReadSettings: ProcessReplySettings();          break;
      }
    } else {
      totalNotProcessedErrors++;
      //Serial1.print("request ignored");
    }

  } else {
    Serial1.print("bad");
    totalCRCErrors++;
  }

  //waitingForReply=false;
}



void PacketReceiveProcessor::ProcessReplyAddressByte() {
  //address byte
  // B X KK AAAA
  // B    = 1 bit broadcast to all modules (in the bank)
  // X    = 1 bit unused
  // KK   = 2 bits module bank select (4 possible banks of 16 modules) - reserved and not used
  // AAAA = 4 bits for address (module id 0 to 15)

  uint8_t broadcast=(_packetbuffer.address & B10000000) >> 7;
  //uint8_t bank=(_packetbuffer.address & B00110000) >> 4;
  //uint8_t lastAddress=_packetbuffer.address & 0x0F;

  //Only set if it was a reply from a broadcast message
  if (broadcast>0) {
    if (numberOfModules[ReplyFromBank()]!=ReplyLastAddress()) {
        numberOfModules[ReplyFromBank()]=ReplyLastAddress();

        //if we have a different number of modules in this bank
        //we should clear all the cached config flags from the modules
        //as they have probably moved address
        for (size_t i = 0; i < maximum_cell_modules; i++)
        {
          cmi[ReplyFromBank()][i].settingsCached=false;
          cmi[ReplyFromBank()][i].voltagemVMin=6000;
          cmi[ReplyFromBank()][i].voltagemVMax=0;
        }
    }
  }
}

void PacketReceiveProcessor::ProcessReplyTemperature() {
  //Called when a decoded packet has arrived in buffer for command 3

  ProcessReplyAddressByte();
  //40 offset for below zero temps
  for (size_t i = 0; i < maximum_cell_modules; i++)
  {
    cmi[ReplyFromBank()][i].internalTemp = ((_packetbuffer.moduledata[i] & 0xFF00)>>8)-40;
    cmi[ReplyFromBank()][i].externalTemp= (_packetbuffer.moduledata[i] & 0x00FF)-40;
  }
}


void PacketReceiveProcessor::ProcessReplyVoltage() {
  //Called when a decoded packet has arrived in _packetbuffer for command 1

  ProcessReplyAddressByte();

  uint8_t b=ReplyFromBank();

  for (size_t i = 0; i < maximum_cell_modules; i++)
  {
    cmi[b][i].voltagemV = _packetbuffer.moduledata[i] & 0x1FFF;
    cmi[b][i].inBypass= (_packetbuffer.moduledata[i] & 0x8000)>0;
    cmi[b][i].bypassOverTemp= (_packetbuffer.moduledata[i] & 0x4000)>0;

    if (cmi[b][i].voltagemV> cmi[b][i].voltagemVMax) {
      cmi[b][i].voltagemVMax=cmi[b][i].voltagemV;
    }

    if (cmi[b][i].voltagemV<cmi[b][i].voltagemVMin) {
      cmi[b][i].voltagemVMin=cmi[b][i].voltagemV;
    }
  }

  //3 top bits remaining
  //X = In bypass
  //Y = Bypass over temperature
  //Z = Not used
}

void PacketReceiveProcessor::ProcessReplySettings() {
  uint8_t b=ReplyFromBank();
  uint8_t m=ReplyLastAddress();

  //TODO Validate b and m here to prevent array overflow
  cmi[b][m].settingsCached=true;
  cmi[b][m].settingsRequested=false;

  cmi[b][m].BypassOverTempShutdown=_packetbuffer.moduledata[3] & 0x00FF;
  cmi[b][m].BypassThresholdmV=_packetbuffer.moduledata[4];
  cmi[b][m].LoadResistance=_packetbuffer.moduledata[0];
  cmi[b][m].Calibration=_packetbuffer.moduledata[1];
  cmi[b][m].mVPerADC=_packetbuffer.moduledata[2];
  cmi[b][m].Internal_BCoefficient=_packetbuffer.moduledata[5];
  cmi[b][m].External_BCoefficient=_packetbuffer.moduledata[6];
}