// tests/unit/test_l3_messages.cpp
//
// Phase 2 TDD: canonical L3 event types and CSV parser.
//
// TEST GROUPS:
//   L3MessageLayout        — struct sizes, alignment, trivially-copyable
//   L3MessageVariant       — variant index ordering, l3_exchange_ts, l3_symbol
//   L3CsvParserHappy       — successful parsing of each message type
//   L3CsvParserLifecycle   — exhaustion, counts, source_name
//   L3CsvParserAdversarial — every validation failure path
//   L3CsvParserFixture     — end-to-end parse of the full fixture session

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <sstream>
#include <string>
#include <type_traits>

#include "quantengine/replay/IL3MarketDataSource.hpp"
#include "quantengine/replay/L3CsvParser.hpp"
#include "quantengine/replay/L3Message.hpp"

namespace quantengine::replay::test {

// ---------------------------------------------------------------------------
// Helper: ParserFixture wraps istringstream + L3CsvParser in a single
// stack object. NOT copyable or movable (holds reference to ss).
// ---------------------------------------------------------------------------
class ParserFixture {
public:
    std::istringstream ss;
    L3CsvParser parser;

    explicit ParserFixture(std::string csv_text, std::string_view name = "test")
        : ss{std::move(csv_text)}, parser{ss, name} {}

    ParserFixture(const ParserFixture&) = delete;
    ParserFixture& operator=(const ParserFixture&) = delete;
    ParserFixture(ParserFixture&&) = delete;
    ParserFixture& operator=(ParserFixture&&) = delete;
};

// ===========================================================================
// L3MessageLayout — struct sizes, alignment, trivially copyable
// ===========================================================================

TEST(L3MessageLayout, OrderAdded_Size56) {
    static_assert(sizeof(OrderAdded) == 56);
    EXPECT_EQ(sizeof(OrderAdded), 56u);
}

TEST(L3MessageLayout, OrderAdded_Align8) {
    static_assert(alignof(OrderAdded) == 8);
    EXPECT_EQ(alignof(OrderAdded), 8u);
}

TEST(L3MessageLayout, OrderAdded_TriviallyCopyable) {
    static_assert(std::is_trivially_copyable_v<OrderAdded>);
    EXPECT_TRUE(std::is_trivially_copyable_v<OrderAdded>);
}

TEST(L3MessageLayout, OrderExecuted_Size48) {
    static_assert(sizeof(OrderExecuted) == 48);
    EXPECT_EQ(sizeof(OrderExecuted), 48u);
}

TEST(L3MessageLayout, OrderExecuted_TriviallyCopyable) {
    static_assert(std::is_trivially_copyable_v<OrderExecuted>);
    EXPECT_TRUE(std::is_trivially_copyable_v<OrderExecuted>);
}

TEST(L3MessageLayout, OrderCancelled_Size40) {
    static_assert(sizeof(OrderCancelled) == 40);
    EXPECT_EQ(sizeof(OrderCancelled), 40u);
}

TEST(L3MessageLayout, OrderCancelled_TriviallyCopyable) {
    static_assert(std::is_trivially_copyable_v<OrderCancelled>);
    EXPECT_TRUE(std::is_trivially_copyable_v<OrderCancelled>);
}

TEST(L3MessageLayout, OrderReplaced_Size64) {
    static_assert(sizeof(OrderReplaced) == 64);
    EXPECT_EQ(sizeof(OrderReplaced), 64u);
}

TEST(L3MessageLayout, OrderReplaced_TriviallyCopyable) {
    static_assert(std::is_trivially_copyable_v<OrderReplaced>);
    EXPECT_TRUE(std::is_trivially_copyable_v<OrderReplaced>);
}

TEST(L3MessageLayout, TradeMessage_Size56) {
    static_assert(sizeof(TradeMessage) == 56);
    EXPECT_EQ(sizeof(TradeMessage), 56u);
}

TEST(L3MessageLayout, TradeMessage_TriviallyCopyable) {
    static_assert(std::is_trivially_copyable_v<TradeMessage>);
    EXPECT_TRUE(std::is_trivially_copyable_v<TradeMessage>);
}

// ===========================================================================
// L3MessageVariant — variant index, helpers
// ===========================================================================

TEST(L3MessageVariant, OrderAdded_IsIndex0) {
    L3Message msg = OrderAdded{};
    EXPECT_EQ(msg.index(), 0u);
    EXPECT_EQ(message_kind(msg), L3MessageKind::OrderAdded);
}

TEST(L3MessageVariant, OrderExecuted_IsIndex1) {
    L3Message msg = OrderExecuted{};
    EXPECT_EQ(msg.index(), 1u);
    EXPECT_EQ(message_kind(msg), L3MessageKind::OrderExecuted);
}

TEST(L3MessageVariant, OrderCancelled_IsIndex2) {
    L3Message msg = OrderCancelled{};
    EXPECT_EQ(msg.index(), 2u);
    EXPECT_EQ(message_kind(msg), L3MessageKind::OrderCancelled);
}

TEST(L3MessageVariant, OrderReplaced_IsIndex3) {
    L3Message msg = OrderReplaced{};
    EXPECT_EQ(msg.index(), 3u);
    EXPECT_EQ(message_kind(msg), L3MessageKind::OrderReplaced);
}

TEST(L3MessageVariant, TradeMessage_IsIndex4) {
    L3Message msg = TradeMessage{};
    EXPECT_EQ(msg.index(), 4u);
    EXPECT_EQ(message_kind(msg), L3MessageKind::TradeMessage);
}

TEST(L3MessageVariant, L3ExchangeTs_ExtractsCorrectly) {
    OrderAdded a{};
    a.exchange_ts = 999'000'000'000LL;
    L3Message msg = a;
    EXPECT_EQ(l3_exchange_ts(msg), 999'000'000'000LL);
}

TEST(L3MessageVariant, L3Symbol_ExtractsCorrectly) {
    OrderExecuted e{};
    e.symbol = market::make_symbol("MSFT");
    L3Message msg = e;
    EXPECT_EQ(l3_symbol(msg), "MSFT");
}

// ===========================================================================
// L3CsvParserHappy — successful parsing of each message type
// ===========================================================================

TEST(L3CsvParserHappy, ParseOrderAdded_AllFields) {
    ParserFixture pf{"A,1000000000,1001,AAPL,15000,100,B"};
    L3Message msg;
    ASSERT_TRUE(pf.parser.next(msg));

    ASSERT_EQ(message_kind(msg), L3MessageKind::OrderAdded);
    const auto& a = std::get<OrderAdded>(msg);
    EXPECT_EQ(a.exchange_ts, 1'000'000'000LL);
    EXPECT_EQ(a.venue_order_id, 1001u);
    EXPECT_EQ(l3_symbol(msg), "AAPL");
    EXPECT_EQ(a.price, 15000LL);
    EXPECT_EQ(a.quantity, 100u);
    EXPECT_EQ(a.side, core::Side::Buy);
}

TEST(L3CsvParserHappy, ParseOrderAdded_SellSide) {
    ParserFixture pf{"A,2000000000,2002,MSFT,28050,75,S"};
    L3Message msg;
    ASSERT_TRUE(pf.parser.next(msg));
    const auto& a = std::get<OrderAdded>(msg);
    EXPECT_EQ(a.side, core::Side::Sell);
    EXPECT_EQ(a.price, 28050LL);
}

TEST(L3CsvParserHappy, ParseOrderExecuted_AllFields) {
    ParserFixture pf{"E,1000000400,1001,AAPL,75,5001"};
    L3Message msg;
    ASSERT_TRUE(pf.parser.next(msg));

    ASSERT_EQ(message_kind(msg), L3MessageKind::OrderExecuted);
    const auto& e = std::get<OrderExecuted>(msg);
    EXPECT_EQ(e.exchange_ts, 1'000'000'400LL);
    EXPECT_EQ(e.venue_order_id, 1001u);
    EXPECT_EQ(e.executed_qty, 75u);
    EXPECT_EQ(e.match_number, 5001u);
    EXPECT_EQ(l3_symbol(msg), "AAPL");
}

TEST(L3CsvParserHappy, ParseOrderCancelled_AllFields) {
    ParserFixture pf{"C,1000000500,1002,AAPL,50"};
    L3Message msg;
    ASSERT_TRUE(pf.parser.next(msg));

    ASSERT_EQ(message_kind(msg), L3MessageKind::OrderCancelled);
    const auto& c = std::get<OrderCancelled>(msg);
    EXPECT_EQ(c.exchange_ts, 1'000'000'500LL);
    EXPECT_EQ(c.venue_order_id, 1002u);
    EXPECT_EQ(c.cancelled_qty, 50u);
    EXPECT_EQ(l3_symbol(msg), "AAPL");
}

TEST(L3CsvParserHappy, ParseOrderReplaced_AllFields) {
    ParserFixture pf{"R,1000000600,1004,1005,AAPL,15075,150,S"};
    L3Message msg;
    ASSERT_TRUE(pf.parser.next(msg));

    ASSERT_EQ(message_kind(msg), L3MessageKind::OrderReplaced);
    const auto& r = std::get<OrderReplaced>(msg);
    EXPECT_EQ(r.exchange_ts, 1'000'000'600LL);
    EXPECT_EQ(r.old_venue_order_id, 1004u);
    EXPECT_EQ(r.new_venue_order_id, 1005u);
    EXPECT_EQ(r.new_price, 15075LL);
    EXPECT_EQ(r.new_quantity, 150u);
    EXPECT_EQ(r.side, core::Side::Sell);
    EXPECT_EQ(l3_symbol(msg), "AAPL");
}

TEST(L3CsvParserHappy, ParseTradeMessage_AllFields) {
    ParserFixture pf{"T,1000000400,5001,AAPL,15000,75,S,0"};
    L3Message msg;
    ASSERT_TRUE(pf.parser.next(msg));

    ASSERT_EQ(message_kind(msg), L3MessageKind::TradeMessage);
    const auto& t = std::get<TradeMessage>(msg);
    EXPECT_EQ(t.exchange_ts, 1'000'000'400LL);
    EXPECT_EQ(t.match_number, 5001u);
    EXPECT_EQ(t.price, 15000LL);
    EXPECT_EQ(t.quantity, 75u);
    EXPECT_EQ(t.aggressor, core::Side::Sell);
    EXPECT_EQ(t.conditions, 0u);
    EXPECT_EQ(l3_symbol(msg), "AAPL");
}

TEST(L3CsvParserHappy, ParseTradeMessage_NonZeroConditions) {
    ParserFixture pf{"T,1000000400,5001,AAPL,15000,75,B,255"};
    L3Message msg;
    ASSERT_TRUE(pf.parser.next(msg));
    EXPECT_EQ(std::get<TradeMessage>(msg).conditions, 255u);
}

TEST(L3CsvParserHappy, FieldsWithLeadingTrailingSpaces_TrimmedOk) {
    // Spaces around commas are stripped by the parser.
    ParserFixture pf{"A, 1000000000, 1001, AAPL, 15000, 100, B"};
    L3Message msg;
    ASSERT_TRUE(pf.parser.next(msg));
    ASSERT_EQ(message_kind(msg), L3MessageKind::OrderAdded);
    const auto& a = std::get<OrderAdded>(msg);
    EXPECT_EQ(a.venue_order_id, 1001u);
    EXPECT_EQ(a.price, 15000LL);
}

TEST(L3CsvParserHappy, WindowsCRLF_StrippedOk) {
    // getline on some systems leaves '\r'; the parser strips it.
    ParserFixture pf{"A,1000000000,1001,AAPL,15000,100,B\r"};
    L3Message msg;
    ASSERT_TRUE(pf.parser.next(msg));
    EXPECT_EQ(message_kind(msg), L3MessageKind::OrderAdded);
    EXPECT_EQ(pf.parser.error_count(), 0u);
}

// ===========================================================================
// L3CsvParserLifecycle — exhaustion, counters, source_name
// ===========================================================================

TEST(L3CsvParserLifecycle, EmptyStream_ExhaustedImmediately) {
    ParserFixture pf{""};
    EXPECT_FALSE(pf.parser.is_exhausted());  // not exhausted before first call
    L3Message msg;
    EXPECT_FALSE(pf.parser.next(msg));
    EXPECT_TRUE(pf.parser.is_exhausted());
    EXPECT_EQ(pf.parser.messages_consumed(), 0u);
}

TEST(L3CsvParserLifecycle, CommentOnlyStream_ZeroMessages) {
    ParserFixture pf{"# this is a comment\n# another comment\n"};
    L3Message msg;
    EXPECT_FALSE(pf.parser.next(msg));
    EXPECT_TRUE(pf.parser.is_exhausted());
    EXPECT_EQ(pf.parser.messages_consumed(), 0u);
    EXPECT_EQ(pf.parser.error_count(), 0u);  // comments are not errors
}

TEST(L3CsvParserLifecycle, BlankLinesSkipped_NoMessages_NoErrors) {
    ParserFixture pf{"\n\n\n"};
    L3Message msg;
    EXPECT_FALSE(pf.parser.next(msg));
    EXPECT_EQ(pf.parser.messages_consumed(), 0u);
    EXPECT_EQ(pf.parser.error_count(), 0u);
}

TEST(L3CsvParserLifecycle, MessagesConsumedIncrements) {
    ParserFixture pf{
        "A,1000000000,1001,AAPL,15000,100,B\n"
        "A,1000000100,1002,AAPL,15000,50,S\n"};
    L3Message msg;
    EXPECT_EQ(pf.parser.messages_consumed(), 0u);
    ASSERT_TRUE(pf.parser.next(msg));
    EXPECT_EQ(pf.parser.messages_consumed(), 1u);
    ASSERT_TRUE(pf.parser.next(msg));
    EXPECT_EQ(pf.parser.messages_consumed(), 2u);
    EXPECT_FALSE(pf.parser.next(msg));
    EXPECT_EQ(pf.parser.messages_consumed(), 2u);  // no change after exhaustion
}

TEST(L3CsvParserLifecycle, SourceNameReturned) {
    ParserFixture pf{"", "my_feed.csv"};
    EXPECT_EQ(pf.parser.source_name(), "my_feed.csv");
}

TEST(L3CsvParserLifecycle, DefaultSourceName_NonEmpty) {
    ParserFixture pf{""};
    EXPECT_FALSE(pf.parser.source_name().empty());
}

TEST(L3CsvParserLifecycle, MultiMessageSequence_CorrectOrder) {
    // Parse 3 different types; verify kind ordering.
    const std::string csv =
        "A,1000000000,1001,AAPL,15000,100,B\n"
        "E,1000000100,1001,AAPL,50,5001\n"
        "C,1000000200,1001,AAPL,50\n";
    ParserFixture pf{csv};
    L3Message msg;

    ASSERT_TRUE(pf.parser.next(msg));
    EXPECT_EQ(message_kind(msg), L3MessageKind::OrderAdded);

    ASSERT_TRUE(pf.parser.next(msg));
    EXPECT_EQ(message_kind(msg), L3MessageKind::OrderExecuted);

    ASSERT_TRUE(pf.parser.next(msg));
    EXPECT_EQ(message_kind(msg), L3MessageKind::OrderCancelled);

    EXPECT_FALSE(pf.parser.next(msg));
    EXPECT_EQ(pf.parser.messages_consumed(), 3u);
    EXPECT_EQ(pf.parser.error_count(), 0u);
}

// ===========================================================================
// L3CsvParserAdversarial — every validation failure path
// ===========================================================================

// ---- OrderAdded failures ---------------------------------------------------

TEST(L3CsvParserAdversarial, OrderAdded_ZeroVenueId_Rejected) {
    // venue_order_id == 0 is the sentinel "invalid" value.
    ParserFixture pf{"A,1000000000,0,AAPL,15000,100,B"};
    L3Message msg;
    EXPECT_FALSE(pf.parser.next(msg));  // skipped → exhausted
    EXPECT_EQ(pf.parser.error_count(), 1u);
    EXPECT_EQ(pf.parser.last_error(), L3CsvParser::ParseStatus::InvalidField);
}

TEST(L3CsvParserAdversarial, OrderAdded_ZeroPrice_Rejected) {
    ParserFixture pf{"A,1000000000,1001,AAPL,0,100,B"};
    L3Message msg;
    EXPECT_FALSE(pf.parser.next(msg));
    EXPECT_EQ(pf.parser.error_count(), 1u);
    EXPECT_EQ(pf.parser.last_error(), L3CsvParser::ParseStatus::InvalidField);
}

TEST(L3CsvParserAdversarial, OrderAdded_NegativePrice_Rejected) {
    // Negative prices are structurally parseable but semantically invalid.
    ParserFixture pf{"A,1000000000,1001,AAPL,-15000,100,B"};
    L3Message msg;
    EXPECT_FALSE(pf.parser.next(msg));
    EXPECT_EQ(pf.parser.error_count(), 1u);
    EXPECT_EQ(pf.parser.last_error(), L3CsvParser::ParseStatus::InvalidField);
}

TEST(L3CsvParserAdversarial, OrderAdded_ZeroQuantity_Rejected) {
    ParserFixture pf{"A,1000000000,1001,AAPL,15000,0,B"};
    L3Message msg;
    EXPECT_FALSE(pf.parser.next(msg));
    EXPECT_EQ(pf.parser.error_count(), 1u);
    EXPECT_EQ(pf.parser.last_error(), L3CsvParser::ParseStatus::InvalidField);
}

TEST(L3CsvParserAdversarial, OrderAdded_WrongFieldCount_TooFew) {
    // A needs 7 fields; this has 6.
    ParserFixture pf{"A,1000000000,1001,AAPL,15000,100"};
    L3Message msg;
    EXPECT_FALSE(pf.parser.next(msg));
    EXPECT_EQ(pf.parser.error_count(), 1u);
    EXPECT_EQ(pf.parser.last_error(), L3CsvParser::ParseStatus::MalformedLine);
}

TEST(L3CsvParserAdversarial, OrderAdded_WrongFieldCount_TooMany) {
    // A needs 7 fields; this has 8.
    ParserFixture pf{"A,1000000000,1001,AAPL,15000,100,B,EXTRA"};
    L3Message msg;
    EXPECT_FALSE(pf.parser.next(msg));
    EXPECT_EQ(pf.parser.error_count(), 1u);
    EXPECT_EQ(pf.parser.last_error(), L3CsvParser::ParseStatus::MalformedLine);
}

TEST(L3CsvParserAdversarial, OrderAdded_InvalidSideChar_Rejected) {
    // Only 'B' and 'S' are valid; 'X' is unknown.
    ParserFixture pf{"A,1000000000,1001,AAPL,15000,100,X"};
    L3Message msg;
    EXPECT_FALSE(pf.parser.next(msg));
    EXPECT_EQ(pf.parser.error_count(), 1u);
    EXPECT_EQ(pf.parser.last_error(), L3CsvParser::ParseStatus::InvalidField);
}

TEST(L3CsvParserAdversarial, OrderAdded_NonNumericPrice_Rejected) {
    ParserFixture pf{"A,1000000000,1001,AAPL,PRICE,100,B"};
    L3Message msg;
    EXPECT_FALSE(pf.parser.next(msg));
    EXPECT_EQ(pf.parser.error_count(), 1u);
    EXPECT_EQ(pf.parser.last_error(), L3CsvParser::ParseStatus::InvalidField);
}

TEST(L3CsvParserAdversarial, OrderAdded_NonNumericVenueId_Rejected) {
    ParserFixture pf{"A,1000000000,VENUE,AAPL,15000,100,B"};
    L3Message msg;
    EXPECT_FALSE(pf.parser.next(msg));
    EXPECT_EQ(pf.parser.last_error(), L3CsvParser::ParseStatus::InvalidField);
}

// ---- Other message type failures -------------------------------------------

TEST(L3CsvParserAdversarial, OrderExecuted_ZeroQuantity_Rejected) {
    ParserFixture pf{"E,1000000000,1001,AAPL,0,5001"};
    L3Message msg;
    EXPECT_FALSE(pf.parser.next(msg));
    EXPECT_EQ(pf.parser.error_count(), 1u);
    EXPECT_EQ(pf.parser.last_error(), L3CsvParser::ParseStatus::InvalidField);
}

TEST(L3CsvParserAdversarial, OrderCancelled_ZeroQuantity_Rejected) {
    ParserFixture pf{"C,1000000000,1001,AAPL,0"};
    L3Message msg;
    EXPECT_FALSE(pf.parser.next(msg));
    EXPECT_EQ(pf.parser.error_count(), 1u);
    EXPECT_EQ(pf.parser.last_error(), L3CsvParser::ParseStatus::InvalidField);
}

TEST(L3CsvParserAdversarial, OrderReplaced_SelfReplace_OldEqualsNew_Rejected) {
    // old_venue_id == new_venue_id is nonsensical.
    ParserFixture pf{"R,1000000000,1004,1004,AAPL,15075,150,S"};
    L3Message msg;
    EXPECT_FALSE(pf.parser.next(msg));
    EXPECT_EQ(pf.parser.error_count(), 1u);
    EXPECT_EQ(pf.parser.last_error(), L3CsvParser::ParseStatus::InvalidField);
}

TEST(L3CsvParserAdversarial, OrderReplaced_ZeroNewPrice_Rejected) {
    ParserFixture pf{"R,1000000000,1004,1005,AAPL,0,150,S"};
    L3Message msg;
    EXPECT_FALSE(pf.parser.next(msg));
    EXPECT_EQ(pf.parser.last_error(), L3CsvParser::ParseStatus::InvalidField);
}

TEST(L3CsvParserAdversarial, TradeMessage_ZeroPrice_Rejected) {
    ParserFixture pf{"T,1000000000,5001,AAPL,0,75,S,0"};
    L3Message msg;
    EXPECT_FALSE(pf.parser.next(msg));
    EXPECT_EQ(pf.parser.last_error(), L3CsvParser::ParseStatus::InvalidField);
}

TEST(L3CsvParserAdversarial, TradeMessage_ZeroQuantity_Rejected) {
    ParserFixture pf{"T,1000000000,5001,AAPL,15000,0,S,0"};
    L3Message msg;
    EXPECT_FALSE(pf.parser.next(msg));
    EXPECT_EQ(pf.parser.last_error(), L3CsvParser::ParseStatus::InvalidField);
}

TEST(L3CsvParserAdversarial, TradeMessage_ConditionsOverflow_Rejected) {
    // conditions is uint8; 256 overflows.
    ParserFixture pf{"T,1000000000,5001,AAPL,15000,75,S,256"};
    L3Message msg;
    EXPECT_FALSE(pf.parser.next(msg));
    EXPECT_EQ(pf.parser.last_error(), L3CsvParser::ParseStatus::InvalidField);
}

// ---- Unknown type ----------------------------------------------------------

TEST(L3CsvParserAdversarial, UnknownTypeChar_Rejected) {
    ParserFixture pf{"X,1000000000,1001,AAPL,15000,100,B"};
    L3Message msg;
    EXPECT_FALSE(pf.parser.next(msg));
    EXPECT_EQ(pf.parser.error_count(), 1u);
    EXPECT_EQ(pf.parser.last_error(), L3CsvParser::ParseStatus::UnknownMessageType);
}

TEST(L3CsvParserAdversarial, EmptyTypeLine_Rejected) {
    // A line with empty type field
    ParserFixture pf{",1000000000,1001,AAPL,15000,100,B"};
    L3Message msg;
    EXPECT_FALSE(pf.parser.next(msg));
    EXPECT_EQ(pf.parser.error_count(), 1u);
}

// ---- Extreme values --------------------------------------------------------

TEST(L3CsvParserAdversarial, ExtremePrice_MaxInt64_Accepted) {
    // INT64_MAX is a valid positive tick value — no reason to reject it.
    const std::int64_t max_price = std::numeric_limits<std::int64_t>::max();
    const std::string csv = "A,1000000000,1001,AAPL," + std::to_string(max_price) + ",100,B";
    ParserFixture pf{csv};
    L3Message msg;
    ASSERT_TRUE(pf.parser.next(msg));
    EXPECT_EQ(std::get<OrderAdded>(msg).price, max_price);
    EXPECT_EQ(pf.parser.error_count(), 0u);
}

TEST(L3CsvParserAdversarial, ExtremeQuantity_MaxUint64_Accepted) {
    const std::uint64_t max_qty = std::numeric_limits<std::uint64_t>::max();
    const std::string csv = "A,1000000000,1001,AAPL,15000," + std::to_string(max_qty) + ",B";
    ParserFixture pf{csv};
    L3Message msg;
    ASSERT_TRUE(pf.parser.next(msg));
    EXPECT_EQ(std::get<OrderAdded>(msg).quantity, max_qty);
    EXPECT_EQ(pf.parser.error_count(), 0u);
}

TEST(L3CsvParserAdversarial, SymbolExceeding15Chars_TruncatedNotRejected) {
    // Symbols > 15 chars are truncated to 15 chars by make_symbol.
    // The parser itself does not reject long symbols.
    ParserFixture pf{"A,1000000000,1001,TOOLONGSYMBOL12,15000,100,B"};
    L3Message msg;
    ASSERT_TRUE(pf.parser.next(msg));
    const auto sym = l3_symbol(msg);
    EXPECT_EQ(sym.size(), 15u);  // truncated to kMaxSymbolLen - 1
    EXPECT_EQ(pf.parser.error_count(), 0u);
}

// ---- Mixed valid/invalid ---------------------------------------------------

TEST(L3CsvParserAdversarial, BadLineThenGoodLine_ErrorCountAndMessage) {
    // One bad line followed by a good line.
    // Parser must skip the bad, return the good, and increment error_count.
    ParserFixture pf{
        "X,bad,line,here\n"
        "A,1000000000,1001,AAPL,15000,100,B\n"};
    L3Message msg;
    ASSERT_TRUE(pf.parser.next(msg));  // skipped bad, returned good
    EXPECT_EQ(message_kind(msg), L3MessageKind::OrderAdded);
    EXPECT_EQ(pf.parser.messages_consumed(), 1u);
    EXPECT_EQ(pf.parser.error_count(), 1u);
}

TEST(L3CsvParserAdversarial, MultipleErrors_ErrorCountAccumulates) {
    // Three bad lines in a row.
    ParserFixture pf{
        "X,bad1\n"
        "A,bad2,fields\n"       // wrong count for A
        "Q,unknown_type,x,x\n"  // unknown type
    };
    L3Message msg;
    EXPECT_FALSE(pf.parser.next(msg));
    EXPECT_EQ(pf.parser.error_count(), 3u);
    EXPECT_EQ(pf.parser.messages_consumed(), 0u);
}

// ===========================================================================
// L3CsvParserFixture — end-to-end parse of the inline fixture session
// ===========================================================================

TEST(L3CsvParserFixture, FullSession_AllTypes_NoErrors) {
    // Mirrors the content of tests/fixtures/l3_sample.csv (subset).
    const std::string session =
        "# header comment\n"
        "\n"
        "A,1000000000,1001,AAPL,15000,100,B\n"
        "A,1000000100,1002,AAPL,15000,50,B\n"
        "A,1000000200,1003,AAPL,15050,75,S\n"
        "A,1000000300,1004,AAPL,15100,200,S\n"
        "E,1000000400,1001,AAPL,75,5001\n"
        "T,1000000400,5001,AAPL,15000,75,S,0\n"
        "C,1000000500,1002,AAPL,50\n"
        "R,1000000600,1004,1005,AAPL,15075,150,S\n"
        "E,1000000700,1005,AAPL,100,5002\n"
        "T,1000000700,5002,AAPL,15075,100,B,0\n"
        "# MSFT\n"
        "A,1000001000,2001,MSFT,28000,200,B\n"
        "A,1000001100,2002,MSFT,28050,100,S\n"
        "E,1000001200,2002,MSFT,100,5003\n";

    ParserFixture pf{session, "l3_sample.csv"};
    L3Message msg;

    // Verify the first 4 are OrderAdded
    for (int i = 0; i < 4; ++i) {
        ASSERT_TRUE(pf.parser.next(msg)) << "at message " << i;
        EXPECT_EQ(message_kind(msg), L3MessageKind::OrderAdded) << "at message " << i;
    }

    ASSERT_TRUE(pf.parser.next(msg));
    EXPECT_EQ(message_kind(msg), L3MessageKind::OrderExecuted);
    EXPECT_EQ(std::get<OrderExecuted>(msg).executed_qty, 75u);

    ASSERT_TRUE(pf.parser.next(msg));
    EXPECT_EQ(message_kind(msg), L3MessageKind::TradeMessage);

    ASSERT_TRUE(pf.parser.next(msg));
    EXPECT_EQ(message_kind(msg), L3MessageKind::OrderCancelled);
    EXPECT_EQ(std::get<OrderCancelled>(msg).cancelled_qty, 50u);

    ASSERT_TRUE(pf.parser.next(msg));
    EXPECT_EQ(message_kind(msg), L3MessageKind::OrderReplaced);
    {
        const auto& r = std::get<OrderReplaced>(msg);
        EXPECT_EQ(r.old_venue_order_id, 1004u);
        EXPECT_EQ(r.new_venue_order_id, 1005u);
        EXPECT_EQ(r.new_price, 15075LL);
    }

    // E at 1000000700
    ASSERT_TRUE(pf.parser.next(msg));
    EXPECT_EQ(message_kind(msg), L3MessageKind::OrderExecuted);
    EXPECT_EQ(std::get<OrderExecuted>(msg).match_number, 5002u);

    // T at 1000000700
    ASSERT_TRUE(pf.parser.next(msg));
    EXPECT_EQ(message_kind(msg), L3MessageKind::TradeMessage);
    EXPECT_EQ(std::get<TradeMessage>(msg).aggressor, core::Side::Buy);

    // MSFT: 2 × A
    ASSERT_TRUE(pf.parser.next(msg));
    EXPECT_EQ(message_kind(msg), L3MessageKind::OrderAdded);
    EXPECT_EQ(l3_symbol(msg), "MSFT");

    ASSERT_TRUE(pf.parser.next(msg));
    EXPECT_EQ(message_kind(msg), L3MessageKind::OrderAdded);
    EXPECT_EQ(l3_symbol(msg), "MSFT");

    // MSFT: E
    ASSERT_TRUE(pf.parser.next(msg));
    EXPECT_EQ(message_kind(msg), L3MessageKind::OrderExecuted);

    // Exhausted
    EXPECT_FALSE(pf.parser.next(msg));
    EXPECT_TRUE(pf.parser.is_exhausted());
    EXPECT_EQ(pf.parser.messages_consumed(), 13u);
    EXPECT_EQ(pf.parser.error_count(), 0u);
}

TEST(L3CsvParserFixture, Determinism_TwoParsersSameInput_IdenticalOutput) {
    // Two independent parsers on the same input must produce identical messages.
    const std::string csv =
        "A,1000000000,1001,AAPL,15000,100,B\n"
        "E,1000000100,1001,AAPL,50,5001\n"
        "C,1000000200,1001,AAPL,50\n";

    ParserFixture pf1{csv};
    ParserFixture pf2{csv};

    L3Message msg1, msg2;
    while (pf1.parser.next(msg1)) {
        ASSERT_TRUE(pf2.parser.next(msg2));
        EXPECT_EQ(msg1, msg2);
    }
    EXPECT_FALSE(pf2.parser.next(msg2));
    EXPECT_EQ(pf1.parser.messages_consumed(), pf2.parser.messages_consumed());
    EXPECT_EQ(pf1.parser.error_count(), pf2.parser.error_count());
}

}  // namespace quantengine::replay::test
