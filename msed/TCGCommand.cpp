/* C:B**************************************************************************
This software is Copyright © 2014 Michael Romeo <r0m30@r0m30.com>

THIS SOFTWARE IS PROVIDED BY THE AUTHORS ''AS IS'' AND ANY EXPRESS
OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 * C:E********************************************************************* */
#include "os.h"
#include <stdio.h>
#ifdef __gnu_linux__
#include <unistd.h>
#endif
#include "TCGCommand.h"
#include "Device.h"
#include "Endianfixup.h"
#include "HexDump.h"
#include "TCGStructures.h"
#include "noparser.h"

/*
 * Initialize: allocate the buffer ONLY reset needs to be called to
 * initialize the headers etc
 */
TCGCommand::TCGCommand()
{
    buffer = (uint8_t *) ALIGNED_ALLOC(4096, IO_BUFFER_LENGTH);
}

/* Fill in the header information and format the call */
TCGCommand::TCGCommand(uint16_t ID, TCG_UID InvokingUid, TCG_METHOD method)
{
    /* allocate the buffer */
    buffer = (uint8_t *) ALIGNED_ALLOC(4096, IO_BUFFER_LENGTH);
    reset(ID, InvokingUid, method);
}

/* Fill in the header information ONLY (no call) */
void
TCGCommand::reset(uint16_t comID)
{
    memset(buffer, 0, IO_BUFFER_LENGTH);
    TCGHeader * hdr;
    hdr = (TCGHeader *) buffer;
    hdr->cp.ExtendedComID[0] = ((comID & 0xff00) >> 8);
    hdr->cp.ExtendedComID[1] = (comID & 0x00ff);
    hdr->cp.ExtendedComID[2] = 0x00;
    hdr->cp.ExtendedComID[3] = 0x00;
    hdr->pkt.TSN = TSN;
    hdr->pkt.HSN = HSN;
    bufferpos = sizeof (TCGHeader);
}

void
TCGCommand::reset(uint16_t comID, TCG_UID InvokingUid, TCG_METHOD method)
{
    reset(comID); // build the headers
    buffer[bufferpos++] = TCG_TOKEN::CALL;
    buffer[bufferpos++] = TCG_SHORT_ATOM::BYTESTRING8;
    memcpy(&buffer[bufferpos], &TCGUID[InvokingUid][0], 8); /* bytes 2-9 */
    bufferpos += 8;
    buffer[bufferpos++] = TCG_SHORT_ATOM::BYTESTRING8;
    memcpy(&buffer[bufferpos], &TCGMETHOD[method][0], 8); /* bytes 11-18 */
    bufferpos += 8;
}

void
TCGCommand::addToken(uint16_t number)
{
    buffer[bufferpos++] = 0x82;
    buffer[bufferpos++] = ((number & 0xff00) >> 8);
    buffer[bufferpos++] = (number & 0x00ff);
}

void
TCGCommand::addToken(const char * bytestring)
{
    if (strlen(bytestring) < 16) {
        /* use tiny atom */
        buffer[bufferpos++] = strlen(bytestring) | 0xa0;

    }
    else if (strlen(bytestring) < 2048) {
        /* Use Medium Atom */
        buffer[bufferpos++] = 0xd0;
        buffer[bufferpos++] = 0x0000 | ((strlen(bytestring)) & 0x00ff);
    }
    else {
        /* Use Large Atom */
        printf("\n\nFAIL missing code -- large atom for bytestring \n");
    }
    memcpy(&buffer[bufferpos], bytestring, (strlen(bytestring)));
    bufferpos += (strlen(bytestring));

}

void
TCGCommand::addToken(TCG_TOKEN token)
{
    buffer[bufferpos++] = token;
}

void
TCGCommand::addToken(TCG_TINY_ATOM token)
{
    buffer[bufferpos++] = token;
}

void
TCGCommand::addToken(TCG_UID token)
{
    buffer[bufferpos++] = TCG_SHORT_ATOM::BYTESTRING8;
    memcpy(&buffer[bufferpos], &TCGUID[token][0], 8);
    bufferpos += 8;
}

void
TCGCommand::complete(uint8_t EOD)
{
    if (EOD) {
        buffer[bufferpos++] = TCG_TOKEN::ENDOFDATA;
        buffer[bufferpos++] = TCG_TOKEN::STARTLIST;
        buffer[bufferpos++] = 0x00;
        buffer[bufferpos++] = 0x00;
        buffer[bufferpos++] = 0x00;
        buffer[bufferpos++] = TCG_TOKEN::ENDLIST;
    }
    /* fill in the lengths and add the modulo 4 padding */
    TCGHeader * hdr;
    hdr = (TCGHeader *) buffer;
    hdr->subpkt.Length = SWAP32(bufferpos - sizeof (TCGHeader));
    while (bufferpos % 4 != 0) {
        buffer[bufferpos++] = 0x00;
    }
    hdr->pkt.Length = SWAP32((bufferpos - sizeof (TCGComPacket))
                             - sizeof (TCGPacket));
    hdr->cp.Length = SWAP32(bufferpos - sizeof (TCGComPacket));
}

uint8_t
TCGCommand::execute(Device * d, void * resp)
{
    uint8_t iorc;
    iorc = SEND(d);
    if (0x00 == iorc)
        iorc = RECV(d, resp);
    return iorc;
}

uint8_t
TCGCommand::SEND(Device * d)
{
    return d->SendCmd(IF_SEND, TCGProtocol, d->comID(), buffer, IO_BUFFER_LENGTH);
}

uint8_t
TCGCommand::RECV(Device * d, void * resp)
{
    return d->SendCmd(IF_RECV, TCGProtocol, d->comID(), resp, IO_BUFFER_LENGTH);
}

uint8_t
TCGCommand::StartSession(Device * device,
                         uint32_t HSN,
                         TCG_UID SP,
                         uint8_t Write,
                         char * HostChallenge,
                         TCG_UID SignAuthority)
{
    int rc = 0;
    reset(device->comID(), TCG_UID::TCG_UID_SMUID, TCG_METHOD::STARTSESSION);
    addToken(TCG_TOKEN::STARTLIST); // [  (Open Bracket)
    addToken(HSN); // HostSessionID : sessionnumber
    addToken(SP); // SPID : SP
    if (Write)
        addToken(TCG_TINY_ATOM::UINT_01);
    else
        addToken(TCG_TINY_ATOM::UINT_00);
    if (NULL != HostChallenge) {
        addToken(TCG_TOKEN::STARTNAME);
        addToken(TCG_TINY_ATOM::UINT_00); // first optional paramater
        addToken(HostChallenge);
        addToken(TCG_TOKEN::ENDNAME);
        addToken(TCG_TOKEN::STARTNAME);
        addToken(TCG_TINY_ATOM::UINT_03); // fourth optional paramater
        addToken(SignAuthority);
        addToken(TCG_TOKEN::ENDNAME);
    }
    addToken(TCG_TOKEN::ENDLIST); // ]  (Close Bracket)
    complete();
    setProtocol(0x01);
    printf("\nDumping StartSession \n");
    dump(); // have a look see
    rc = SEND(device);
    if (0 != rc) {
        printf("StartSession failed %d on send", rc);
        return rc;
    }
    //    Sleep(250);
    memset(buffer, 0, IO_BUFFER_LENGTH);
    rc = RECV(device, buffer);
    if (0 != rc) {
        printf("StartSession failed %d on recv", rc);
        return rc;
    }
    printf("\nDumping StartSession Reply (SyncSession)\n");
    dump();
    SSResponse * ssresp = (SSResponse *) buffer;
    setHSN(ssresp->HostSessionNumber);
    setTSN(ssresp->TPerSessionNumber);
}

uint8_t
TCGCommand::EndSession(Device * device)
{
    int rc = 0;
    reset(device->comID());
    addToken(TCG_TOKEN::ENDOFSESSION); // [  (Open Bracket)
    complete(0);
    setHSN(0);
    setTSN(0);
    rc = SEND(device);
    if (0 != rc) {
        printf("EndSession failed %d on send", rc);
        return rc;
    }
    memset(buffer, 0, IO_BUFFER_LENGTH);
    rc = RECV(device, buffer);
    if (0 != rc) {
        printf("EndSession failed %d on recv", rc);
        return rc;
    }
    printf("\nDumping EndSession Reply\n");
    dump();
    return 0;
}

void
TCGCommand::setHSN(uint32_t value)
{
    HSN = value;
}

void
TCGCommand::setTSN(uint32_t value)
{
    TSN = value;
}

void
TCGCommand::setProtocol(uint8_t value)
{
    TCGProtocol = value;
}

void
TCGCommand::dump()
{
    printf("\n TCGCommand buffer\n");
    HexDump(buffer, bufferpos);
}

TCGCommand::~TCGCommand()
{
    ALIGNED_FREE(buffer);
}
