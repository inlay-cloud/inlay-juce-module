#include "SignatureVerification.h"

#include <array>
#include <cstring>

#include <juce_cryptography/juce_cryptography.h>

namespace inlay::internal {
namespace
{
constexpr std::array<juce::uint8, 19> sha256DigestInfoPrefix{
    0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01,
    0x65, 0x03, 0x04, 0x02, 0x01, 0x05, 0x00, 0x04, 0x20
};

juce::MemoryBlock toLittleEndian (const juce::MemoryBlock& bigEndian)
{
    juce::MemoryBlock littleEndian (bigEndian.getSize());
    const auto* source = static_cast<const juce::uint8*> (bigEndian.getData());
    auto* dest = static_cast<juce::uint8*> (littleEndian.getData());

    for (size_t i = 0; i < bigEndian.getSize(); ++i)
        dest[i] = source[bigEndian.getSize() - 1 - i];

    return littleEndian;
}

juce::MemoryBlock toBigEndianPadded (const juce::BigInteger& value, size_t sizeInBytes)
{
    const auto littleEndian = value.toMemoryBlock();

    if (littleEndian.getSize() > sizeInBytes)
        return {};

    juce::MemoryBlock bigEndian (sizeInBytes, true);
    const auto* source = static_cast<const juce::uint8*> (littleEndian.getData());
    auto* dest = static_cast<juce::uint8*> (bigEndian.getData());
    const auto padding = sizeInBytes - littleEndian.getSize();

    for (size_t i = 0; i < littleEndian.getSize(); ++i)
        dest[padding + i] = source[littleEndian.getSize() - 1 - i];

    return bigEndian;
}

size_t getRSAKeySizeInBytes (const juce::String& publicKey)
{
    juce::BigInteger modulus;
    modulus.parseString (publicKey.fromFirstOccurrenceOf (",", false, false), 16);

    return static_cast<size_t> ((modulus.getHighestBit() + 8) >> 3);
}
} // namespace

bool verifyRSASHA256Signature (const juce::MemoryBlock& payload,
                               const juce::MemoryBlock& signature,
                               const juce::String& publicKey)
{
    if (payload.getSize() == 0 || signature.getSize() == 0 || publicKey.isEmpty())
        return false;

    juce::RSAKey key (publicKey);
    if (! key.isValid())
        return false;

    const auto keySizeInBytes = getRSAKeySizeInBytes (publicKey);
    if (keySizeInBytes == 0 || signature.getSize() != keySizeInBytes)
        return false;

    juce::BigInteger signatureValue;
    signatureValue.loadFromMemoryBlock (toLittleEndian (signature));

    if (! key.applyToValue (signatureValue))
        return false;

    const auto encodedMessage = toBigEndianPadded (signatureValue, keySizeInBytes);
    if (encodedMessage.getSize() != keySizeInBytes)
        return false;

    const auto hash = juce::SHA256 (payload).getRawData();
    juce::MemoryBlock expectedDigestInfo;
    expectedDigestInfo.append (sha256DigestInfoPrefix.data(), sha256DigestInfoPrefix.size());
    expectedDigestInfo.append (hash.getData(), hash.getSize());

    if (keySizeInBytes < expectedDigestInfo.getSize() + 11)
        return false;

    const auto* bytes = static_cast<const juce::uint8*> (encodedMessage.getData());
    if (bytes[0] != 0x00 || bytes[1] != 0x01)
        return false;

    int separatorIndex = -1;
    for (size_t i = 2; i < encodedMessage.getSize(); ++i)
        if (bytes[i] == 0x00)
        {
            separatorIndex = static_cast<int> (i);
            break;
        }

    if (separatorIndex < 10
        || static_cast<size_t> (separatorIndex) + 1 + expectedDigestInfo.getSize() != keySizeInBytes)
        return false;

    for (int i = 2; i < separatorIndex; ++i)
        if (bytes[i] != 0xff)
            return false;

    return std::memcmp (bytes + separatorIndex + 1,
                        expectedDigestInfo.getData(),
                        expectedDigestInfo.getSize()) == 0;
}
} // namespace inlay::internal
