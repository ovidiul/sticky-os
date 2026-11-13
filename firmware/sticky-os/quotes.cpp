#include "quotes.h"

#include <HTTPClient.h>
#include <LittleFS.h>
#include <WiFiClientSecure.h>

#include "glyphs.h"
#include "i18n.h"
#include "json.h"
#include "net.h"

// Public domain: each author died long ago and these words were published
// long ago (translations: Jowett, Legge, Long, Gummere, Townsend, all
// nineteenth or early twentieth century). Checked against the works they
// come from; the popular misattributions ("Be yourself; everyone else is
// already taken", "Go confidently in the direction of your dreams"...) are
// left out on purpose. In flash, not RAM.
struct QuoteEntry { const char *text, *author; };
static const QuoteEntry OFFLINE[] = {
    {"The unexamined life is not worth living.", "Socrates"},
    {"This above all: to thine own self be true.", "William Shakespeare"},
    {"There is nothing either good or bad, but thinking makes it so.", "William Shakespeare"},
    {"We know what we are, but know not what we may be.", "William Shakespeare"},
    {"Be not afraid of greatness.", "William Shakespeare"},
    {"Brevity is the soul of wit.", "William Shakespeare"},
    {"Hope is the thing with feathers that perches in the soul.", "Emily Dickinson"},
    {"Forever is composed of nows.", "Emily Dickinson"},
    {"Nothing is at last sacred but the integrity of your own mind.", "Ralph Waldo Emerson"},
    {"Nothing great was ever achieved without enthusiasm.", "Ralph Waldo Emerson"},
    {"The only way to have a friend is to be one.", "Ralph Waldo Emerson"},
    {"Trust thyself: every heart vibrates to that iron string.", "Ralph Waldo Emerson"},
    {"Our life is frittered away by detail. Simplify, simplify.", "Henry David Thoreau"},
    {"Heaven is under our feet as well as over our heads.", "Henry David Thoreau"},
    {"If one advances confidently in the direction of his dreams, and endeavors to live the life which he has imagined, "
     "he will meet with a success unexpected in common hours.", "Henry David Thoreau"},
    {"Well done is better than well said.", "Benjamin Franklin"},
    {"Lost time is never found again.", "Benjamin Franklin"},
    {"Little strokes fell great oaks.", "Benjamin Franklin"},
    {"With malice toward none, with charity for all.", "Abraham Lincoln"},
    {"Optimism is the faith that leads to achievement.", "Helen Keller"},
    {"We are all in the gutter, but some of us are looking at the stars.", "Oscar Wilde"},
    {"Experience is the name every one gives to their mistakes.", "Oscar Wilde"},
    {"Always do right. This will gratify some people and astonish the rest.", "Mark Twain"},
    {"Courage is resistance to fear, mastery of fear, not absence of fear.", "Mark Twain"},
    {"I am large, I contain multitudes.", "Walt Whitman"},
    {"There is no charm equal to tenderness of heart.", "Jane Austen"},
    {"No one is useless in this world who lightens the burden of it for any one else.", "Charles Dickens"},
    {"To travel hopefully is a better thing than to arrive.", "Robert Louis Stevenson"},
    {"Learning without thought is labour lost; thought without learning is perilous.", "Confucius"},
    {"Very little indeed is necessary for living a happy life.", "Marcus Aurelius"},
    {"The universe is transformation; life is opinion.", "Marcus Aurelius"},
    {"Men are disturbed not by the things which happen, but by the opinions about the things.", "Epictetus"},
    {"Begin at once to live, and count each separate day as a separate life.", "Seneca"},
    {"It is not the man who has too little, but the man who craves more, that is poor.", "Seneca"},
    {"Slow but steady wins the race.", "Aesop"},
    {"To err is human, to forgive divine.", "Alexander Pope"},
    {"Hope springs eternal in the human breast.", "Alexander Pope"},
    {"A thing of beauty is a joy for ever.", "John Keats"},
    {"To see a World in a Grain of Sand, and a Heaven in a Wild Flower.", "William Blake"},
    {"Two roads diverged in a wood, and I took the one less traveled by, and that has made all the difference.", "Robert Frost"},
    {"The woods are lovely, dark and deep, but I have promises to keep, and miles to go before I sleep.", "Robert Frost"},
    {"Why, sometimes I've believed as many as six impossible things before breakfast.", "Lewis Carroll"},
    {"It's no use going back to yesterday, because I was a different person then.", "Lewis Carroll"},
    {"I am no bird; and no net ensnares me.", "Charlotte Bronte"},
    {"Whatever our souls are made of, his and mine are the same.", "Emily Bronte"},
    {"I'm not afraid of storms, for I'm learning how to sail my ship.", "Louisa May Alcott"},
    {"Great works are performed, not by strength, but perseverance.", "Samuel Johnson"},
    {"Whatever is worth doing at all is worth doing well.", "Lord Chesterfield"},
    {"In every walk with nature one receives far more than he seeks.", "John Muir"},
    {"Work is love made visible.", "Kahlil Gibran"},
    {"Faith is the bird that feels the light and sings when the dawn is still dark.", "Rabindranath Tagore"},
    {"All that we see or seem is but a dream within a dream.", "Edgar Allan Poe"},
    {"Nothing is so painful to the human mind as a great and sudden change.", "Mary Shelley"},
    {"It is not the critic who counts; not the man who points out how the strong man stumbles, or where the doer of "
     "deeds could have done them better.", "Theodore Roosevelt"},
};
static const int OFFLINE_N = sizeof(OFFLINE) / sizeof(OFFLINE[0]);

int quotesOfflineCount() { return OFFLINE_N; }

Quote quoteOffline(int i) {
  i = (i % OFFLINE_N + OFFLINE_N) % OFFLINE_N;
  return {OFFLINE[i].text, OFFLINE[i].author, 0, QUOTE_OFFLINE};
}

// Days since 1970 from 20261113, so consecutive days get consecutive quotes.
static long dayNumber(long date) {
  int y = date / 10000, m = date / 100 % 100, d = date % 100;
  y -= m <= 2;
  long era = (y >= 0 ? y : y - 399) / 400, yoe = y - era * 400;
  long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1, doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

Quote quoteOfflineForDay(long date) {
  Quote q = quoteOffline(dayNumber(date) * 7);  // 7 and 54 share no factor: every quote comes round
  q.date = date;
  return q;
}

Quote quoteOfflineRandom(const String &notThis) {
  Quote q;
  for (int tries = 0; tries < 8; tries++) {
    q = quoteOffline(esp_random() % OFFLINE_N);
    if (q.text != notThis) break;
  }
  return q;
}

// ---------------------------------------------------------------- the cache

static const char *CACHE = "/quote.txt";

// Four lines: the date, where it came from, the words, the author.
bool quoteLoadCache(Quote &q) {
  File f = LittleFS.open(CACHE, "r");
  if (!f) return false;
  String date = f.readStringUntil('\n'), from = f.readStringUntil('\n');
  q.text = f.readStringUntil('\n');
  q.author = f.readStringUntil('\n');
  f.close();
  q.date = date.toInt();
  q.from = from.toInt();
  return q.date > 20000000 && q.text.length();
}

void quoteSaveCache(const Quote &q) {
  File f = LittleFS.open(CACHE, "w");
  if (!f) return;
  f.printf("%ld\n%u\n%s\n%s\n", q.date, q.from, q.text.c_str(), q.author.c_str());
  f.close();
}

// ---------------------------------------------------------------- ZenQuotes

uint8_t fetchQuote(bool today, Quote &q, String &error) {
  error = "";
  if (!netUp()) return error = T(S_N_NO_WIFI), QUOTE_FAILED;
  NetTls use;
  WiFiClientSecure tls;
  netTrust(tls, false);  // the public CAs (zenquotes.io is on Let's Encrypt), and the 8 s handshake
  tls.setTimeout(10000);
  HTTPClient http;
  http.setTimeout(10000);
  http.setConnectTimeout(NET_CONNECT_MS);
  if (!http.begin(tls, today ? "https://zenquotes.io/api/today" : "https://zenquotes.io/api/random"))
    return error = T(S_N_QUOTE_BAD_ADDRESS), QUOTE_FAILED;
  uint32_t start = millis();
  int status = http.GET();
  if (status == HTTPC_ERROR_CONNECTION_REFUSED && millis() - start >= NET_HANDSHAKE_S * 1000UL - 500) status = HTTPC_ERROR_READ_TIMEOUT;
  String body;
  bool read = (status == 200 || status == 429) && netReadBody(http, body, 4096);
  http.end();
  Serial.printf("quote: %s answered %d, %u bytes\n", today ? "today" : "random", status, body.length());
  // Past the limit ZenQuotes answers 429 with a quote of its own: "Too many requests...", by zenquotes.io.
  String a = jsonString(body, "a"), t = jsonString(body, "q");
  if (status == 429 || a == "zenquotes.io" || t.startsWith("Too many requests")) return QUOTE_LIMITED;
  if (status != 200 || !read) {
    error = netFailText("ZenQuotes", status == 200 ? NET_ERR_BAD_BODY : status);
    return QUOTE_FAILED;
  }
  t = textFold(t);
  a = textFold(a);
  t.trim();
  a.trim();
  if (!t.length() || t.length() > 600) return error = TF(S_N_FAIL_UNREADABLE, "ZenQuotes"), QUOTE_FAILED;
  q.text = t;
  q.author = a.length() ? a : String(T(S_N_QUOTE_UNKNOWN));
  q.date = localDate(time(nullptr));
  q.from = today ? QUOTE_ZEN_TODAY : QUOTE_ZEN_RANDOM;
  return QUOTE_GOT;
}
