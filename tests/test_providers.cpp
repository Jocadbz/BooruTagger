#include "HttpFixture.h"
#include "providers/DanbooruProvider.h"
#include "providers/ShimmieProvider.h"
#include <QJsonArray>
#include <QStandardPaths>
#include <QUrlQuery>
#include <QtTest>

using namespace bt;

class ProviderTests : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }
    void danbooruContract()
    {
        HttpFixture server;
        server.handler = [](const FixtureRequest &request) -> FixtureResponse {
            if (request.url.path().endsWith("profile.json"))
                return {200, R"({"id":42,"name":"tester"})", {}, false};
            if (request.url.path().endsWith("tags.json"))
                return {200, R"([{"name":"smile","category":0,"post_count":123,"is_deprecated":false}])", {}, false};
            if (request.url.path().endsWith("tag_aliases.json"))
                return {
                    200, R"([{"antecedent_name":"smiling","consequent_name":"smile","status":"active"}])", {}, false};
            if (request.url.path().endsWith("posts.json"))
                return {200,
                        R"([{"id":8,"tag_string":"solo tagme","file_url":"/media/a.jpg","file_ext":"jpg"}])",
                        {},
                        false};
            return {200, R"({"id":8,"tag_string":"solo smile","file_url":null,"file_ext":"webm"})", {}, false};
        };
        DanbooruProvider provider({"dan-test", "test", ProviderKind::Danbooru, server.base("/booru"), "tester", true});
        std::optional<Result<AuthInfo>> auth;
        provider.authenticate("secret", [&](auto result) { auth = result; });
        QTRY_VERIFY(auth.has_value());
        QVERIFY(*auth);
        QCOMPARE(server.requests.first().headers.value("authorization"), QByteArray("Basic dGVzdGVyOnNlY3JldA=="));
        QVERIFY(!server.requests.first().url.toString().contains("secret"));
        std::optional<Result<Post>> post;
        provider.fetchPost(8, [&](auto result) { post = result; });
        QTRY_VERIFY(post.has_value());
        QVERIFY(*post);
        QVERIFY(post->value->mediaUrl.isEmpty());
        QVERIFY(server.requests.last().headers.value("user-agent").contains("user #42"));
        std::optional<Result<SearchPage>> search;
        provider.searchPosts({"tagme", 1, "b9", 0}, [&](auto result) { search = result; });
        QTRY_VERIFY(search.has_value());
        QVERIFY(*search);
        QCOMPARE(search->value->nextCursor, QString("b8"));
        const QUrlQuery query(server.requests.last().url);
        QCOMPARE(query.queryItemValue("page"), QString("b9"));
        QCOMPARE(query.queryItemValue("tags"), QString("tagme order:id_desc"));
        std::optional<Result<QList<TagSuggestion>>> tags;
        provider.searchTags("sm", [&](auto result) { tags = result; });
        QTRY_VERIFY(tags.has_value());
        QVERIFY(*tags);
        QCOMPARE(tags->value->first().alias, QString("smiling"));
        QCOMPARE(tags->value->first().postCount.value(), 123);
        std::optional<Result<bool>> update;
        provider.updateTags(8, {"solo", "smile"}, {"solo", "tagme"}, [&](auto result) { update = result; });
        QTRY_VERIFY(update.has_value());
        QVERIFY(*update);
        const auto request = server.requests.last();
        QCOMPARE(request.method, QByteArray("PUT"));
        const auto body = request.json().value("post").toObject();
        QCOMPARE(body.size(), 2);
        QCOMPARE(body.value("old_tag_string").toString(), QString("solo tagme"));
        QCOMPARE(body.value("tag_string").toString(), QString("smile solo"));
    }
    void parsing()
    {
        QVERIFY(!parseJson("not json"));
        QVERIFY(!DanbooruProvider::parsePost({{"id", 1}}, QUrl("https://example.test")));
        QVERIFY(!ShimmieProvider::parsePost({{"id", 1}, {"tags", "solo"}}, QUrl("https://example.test")));
        auto post = ShimmieProvider::parsePost({{"id", "opaque"},
                                                {"post_id", "123"},
                                                {"tags", QJsonArray{"Custom:Tag", "solo"}},
                                                {"image_link", "images/a.webm"},
                                                {"ext", "webm"}},
                                               QUrl("https://example.test/booru/"));
        QVERIFY(post);
        QCOMPARE(post.value->id, 123);
        QCOMPARE(post.value->mediaUrl.toString(), QString("https://example.test/booru/images/a.webm"));
        QVERIFY(post.value->tags.contains("Custom:Tag"));
        const auto error = ShimmieProvider::parseEnvelope(
            R"({"data":{"posts":[]},"errors":[{"message":"denied","extensions":{"code":"FORBIDDEN"}}]})");
        QVERIFY(!error);
        QCOMPARE(error.error.kind, ErrorKind::Permission);
        QVERIFY(!ShimmieProvider::parseEnvelope(R"({"data":null})"));
    }
    void shimmieContractAndEscaping()
    {
        HttpFixture server;
        server.handler = [](const FixtureRequest &request) -> FixtureResponse {
            const auto query = request.json().value("query").toString();
            if (query.contains("__schema"))
                return {200, R"({"errors":[{"message":"introspection disabled"}]})", {}, false};
            if (query.contains("login("))
                return {200, R"({"data":{"login":{"user":{"name":"tester"},"session":"private-token"}}})", {}, false};
            if (query.contains("update_post_metadata("))
                return {
                    200, R"({"data":{"update_post_metadata":{"id":"opaque","tags":"Custom:Tag solo"}}})", {}, false};
            return {
                200,
                R"({"data":{"posts":[{"id":9999,"post_id":123,"tags":["Custom:Tag","tagme"],"ext":"png","image_link":"/image/123"}]}})",
                {},
                false};
        };
        ShimmieProvider provider({"shm-test", "test", ProviderKind::Shimmie, server.base("/subdir/"), "tester", true});
        std::optional<Result<AuthInfo>> auth;
        provider.authenticate("pass\"word\\injection", [&](auto result) { auth = result; });
        QTRY_VERIFY(auth.has_value());
        QVERIFY(*auth);
        QCOMPARE(server.requests.last().url.path(), QString("/subdir/graphql"));
        const auto loginQuery = server.requests.last().json().value("query").toString();
        QVERIFY(loginQuery.contains("pass\\\"word\\\\injection"));
        std::optional<Result<Post>> post;
        provider.fetchPost(123, [&](auto result) { post = result; });
        QTRY_VERIFY(post.has_value());
        QVERIFY(*post);
        QCOMPARE(post->value->id, 123);
        const auto cookie = server.requests.last().headers.value("cookie");
        QVERIFY(cookie.contains("shm_user=tester"));
        QVERIFY(cookie.contains("shm_session=private-token"));
        QVERIFY(server.requests.last().json().value("query").toString().contains("id=123"));
        std::optional<Result<bool>> update;
        provider.updateTags(123, {"Custom:Tag", "solo"}, {"tagme"}, [&](auto result) { update = result; });
        QTRY_VERIFY(update.has_value());
        QVERIFY(*update);
        const auto mutation = server.requests.last().json().value("query").toString();
        QVERIFY(mutation.contains("update_post_metadata("));
        QVERIFY(mutation.contains("post_id:123"));
        QVERIFY(mutation.contains("key:\"tags\""));
        QVERIFY(!mutation.contains("source:"));
        QVERIFY(provider.remoteAutocomplete());
        QVERIFY(provider.prefetchTagIndex());
        QCOMPARE(provider.http()->redact("private-token"), QString("[redacted]"));
    }
    void shimmieVariables()
    {
        HttpFixture server;
        server.handler = [](const FixtureRequest &request) -> FixtureResponse {
            if (request.json().value("query").toString().contains("__schema"))
                return {
                    200,
                    R"({"data":{"__schema":{"mutationType":{"fields":[{"name":"login","args":[{"name":"username","type":{"kind":"NON_NULL","ofType":{"kind":"SCALAR","name":"String"}}},{"name":"password","type":{"kind":"NON_NULL","ofType":{"kind":"SCALAR","name":"String"}}}]}]},"queryType":{"fields":[]}}}})",
                    {},
                    false};
            return {200, R"({"data":{"login":{"user":{"name":"tester"},"session":"token"}}})", {}, false};
        };
        ShimmieProvider provider({"shm-vars", "test", ProviderKind::Shimmie, server.base(), "tester", true});
        std::optional<Result<AuthInfo>> auth;
        provider.authenticate("secret", [&](auto result) { auth = result; });
        QTRY_VERIFY(auth.has_value());
        QVERIFY(*auth);
        const auto body = server.requests.last().json();
        QVERIFY(body.value("query").toString().contains("$password:String!"));
        QVERIFY(!body.value("query").toString().contains("secret"));
        QCOMPARE(body.value("variables").toObject().value("password").toString(), QString("secret"));
    }
    void shimmieTagSearchUsesOuroboros()
    {
        HttpFixture server;
        server.handler = [](const FixtureRequest &request) -> FixtureResponse {
            if (request.url.path().endsWith("/post/index.json"))
                return {
                    200,
                    R"([{"id":73,"tags":"tagme solo","file_url":"media/73.png","preview_url":"thumb/73.png","width":640,"height":480,"file_ext":"png"}])",
                    {},
                    false};
            const auto query = request.json().value("query").toString();
            if (query.contains("__schema"))
                return {
                    200,
                    R"({"data":{"__schema":{"queryType":{"fields":[{"name":"post","args":[{"name":"post_id","type":{"kind":"NON_NULL","ofType":{"kind":"SCALAR","name":"Int"}}}]}]},"mutationType":{"fields":[{"name":"login","args":[{"name":"username","type":{"kind":"SCALAR","name":"String"}},{"name":"password","type":{"kind":"SCALAR","name":"String"}}]}]}}}})",
                    {},
                    false};
            if (query.contains("login("))
                return {200, R"({"data":{"login":{"user":{"name":"tester"},"session":"search-token"}}})", {}, false};
            return {200, R"({"data":{}})", {}, false};
        };
        ShimmieProvider provider(
            {"shm-search", "test", ProviderKind::Shimmie, server.base("/archive/"), "tester", true});
        std::optional<Result<AuthInfo>> auth;
        provider.authenticate("password", [&](auto result) { auth = result; });
        QTRY_VERIFY(auth.has_value());
        QVERIFY(*auth);

        std::optional<Result<SearchPage>> search;
        provider.searchPosts({"tagme", 20, {}, 0}, [&](auto result) { search = result; });
        QTRY_VERIFY(search.has_value());
        QVERIFY(*search);
        QCOMPARE(search->value->posts.size(), 1);
        QCOMPARE(search->value->posts.first().id, 73);
        QCOMPARE(search->value->posts.first().tags, QStringList({"tagme", "solo"}));
        QCOMPARE(search->value->posts.first().mediaUrl.toString(),
                 QString("http://127.0.0.1:%1/archive/media/73.png").arg(server.serverPort()));
        QCOMPARE(search->value->posts.first().previewUrl.toString(),
                 QString("http://127.0.0.1:%1/archive/thumb/73.png").arg(server.serverPort()));
        const auto request = server.requests.last();
        QCOMPARE(request.method, QByteArray("GET"));
        QCOMPARE(request.url.path(), QString("/archive/post/index.json"));
        const QUrlQuery query(request.url);
        QCOMPARE(query.queryItemValue("tags"), QString("tagme"));
        QCOMPARE(query.queryItemValue("limit"), QString("20"));
        QCOMPARE(query.queryItemValue("page"), QString("1"));
    }
    void shimmieLoadsPopularTags()
    {
        HttpFixture server;
        server.handler = [](const FixtureRequest &request) -> FixtureResponse {
            if (request.url.path().endsWith("/tag/index.json"))
                return {
                    200,
                    R"([{"id":4,"name":"solo","count":234,"type":0,"ambiguous":false},{"id":5,"name":"tagme","count":98,"type":0,"ambiguous":false}])",
                    {},
                    false};
            return {200, "[]", {}, false};
        };
        ShimmieProvider provider(
            {"shm-popular", "test", ProviderKind::Shimmie, server.base("/gallery/"), "tester", true});
        std::optional<Result<QList<TagSuggestion>>> tags;
        provider.searchTags("solo", [&](auto result) { tags = result; });
        QTRY_VERIFY(tags.has_value());
        QVERIFY(*tags);
        QCOMPARE(tags->value->size(), 2);
        QCOMPARE(tags->value->first().name, QString("solo"));
        QCOMPARE(tags->value->first().postCount.value(), 234);
        QVERIFY(!tags->value->first().category);
        const auto request = server.requests.first();
        QCOMPARE(request.method, QByteArray("GET"));
        QCOMPARE(request.url.path(), QString("/gallery/tag/index.json"));
        const QUrlQuery query(request.url);
        QCOMPARE(query.queryItemValue("order"), QString("count"));
        QCOMPARE(query.queryItemValue("limit"), QString("200"));
        QCOMPARE(query.queryItemValue("page"), QString("1"));

        tags.reset();
        provider.searchTags("tagme", [&](auto result) { tags = result; });
        QTRY_VERIFY(tags.has_value());
        QVERIFY(*tags);
        QCOMPARE(server.requests.size(), 1);
        QCOMPARE(tags->value->size(), 2);
    }
    void shimmieSinglePostSchema()
    {
        HttpFixture server;
        server.handler = [](const FixtureRequest &request) -> FixtureResponse {
            if (request.url.path().endsWith("/post/index.json"))
                return {200, "<html><body>No handler for post/index.json</body></html>", {}, false};
            const auto query = request.json().value("query").toString();
            if (query.contains("__schema"))
                return {
                    200,
                    R"({"data":{"__schema":{"queryType":{"fields":[{"name":"post","args":[{"name":"id","type":{"kind":"NON_NULL","ofType":{"kind":"SCALAR","name":"Int"}}}]}]},"mutationType":{"fields":[{"name":"login","args":[{"name":"username","type":{"kind":"SCALAR","name":"String"}},{"name":"password","type":{"kind":"SCALAR","name":"String"}}]}]}}}})",
                    {},
                    false};
            if (query.contains("login("))
                return {200, R"({"data":{"login":{"user":{"name":"tester"},"session":"singular-token"}}})", {}, false};
            if (query.contains("post("))
                return {
                    200,
                    R"({"data":{"post":{"id":987,"post_id":123,"tags":["solo","Custom:Tag"],"ext":"png","image_link":"media/123.png"}}})",
                    {},
                    false};
            if (query.contains("posts("))
                return {200,
                        "{\"errors\":[{\"message\":\"Cannot query field \\\"posts\\\" on type \\\"Query\\\". Did you "
                        "mean \\\"post\\\"?\"}]}",
                        {},
                        false};
            return {200, R"({"data":{}})", {}, false};
        };
        ShimmieProvider provider(
            {"shm-singular", "test", ProviderKind::Shimmie, server.base("/gallery/"), "tester", true});
        std::optional<Result<AuthInfo>> auth;
        provider.authenticate("password", [&](auto result) { auth = result; });
        QTRY_VERIFY(auth.has_value());
        QVERIFY(*auth);
        std::optional<Result<Post>> post;
        provider.fetchPost(123, [&](auto result) { post = result; });
        QTRY_VERIFY(post.has_value());
        QVERIFY(*post);
        QCOMPARE(post->value->id, 123);
        QCOMPARE(post->value->tags, QStringList({"solo", "Custom:Tag"}));
        QCOMPARE(post->value->mediaUrl.toString(),
                 QString("http://127.0.0.1:%1/gallery/media/123.png").arg(server.serverPort()));
        QCOMPARE(server.requests.size(), 3);
        const auto singleQuery = server.requests.last().json();
        QVERIFY(singleQuery.value("query").toString().contains("$id:Int!"));
        QVERIFY(singleQuery.value("query").toString().contains("post(id:$id)"));
        QVERIFY(!singleQuery.value("query").toString().contains("posts("));
        QCOMPARE(singleQuery.value("variables").toObject().value("id").toInt(), 123);

        const auto requestCount = server.requests.size();
        std::optional<Result<SearchPage>> search;
        provider.searchPosts({"tagme", 20, {}, 0}, [&](auto result) { search = result; });
        QTRY_VERIFY(search.has_value());
        QVERIFY(!*search);
        QCOMPARE(search->error.kind, ErrorKind::InvalidData);
        QVERIFY(search->error.message.contains("ouroboros_api"));
        QCOMPARE(server.requests.size(), requestCount + 1);
    }
    void shimmieRecoversFromUnsupportedPostsError()
    {
        HttpFixture server;
        int schemaRequests = 0;
        server.handler = [&schemaRequests](const FixtureRequest &request) -> FixtureResponse {
            const auto query = request.json().value("query").toString();
            if (query.contains("__schema")) {
                ++schemaRequests;
                if (schemaRequests == 1)
                    return {200, R"({"errors":[{"message":"introspection was not available yet"}]})", {}, false};
                return {
                    200,
                    R"({"data":{"__schema":{"queryType":{"fields":[{"name":"post","args":[{"name":"post_id","type":{"kind":"SCALAR","name":"Int"}}]}]},"mutationType":{"fields":[{"name":"login","args":[{"name":"username","type":{"kind":"SCALAR","name":"String"}},{"name":"password","type":{"kind":"SCALAR","name":"String"}}]}]}}}})",
                    {},
                    false};
            }
            if (query.contains("login("))
                return {200, R"({"data":{"login":{"user":{"name":"tester"},"session":"recovered-token"}}})", {}, false};
            if (query.contains("posts("))
                return {200,
                        "{\"errors\":[{\"message\":\"Cannot query field \\\"posts\\\" on type \\\"Query\\\". Did you "
                        "mean \\\"post\\\"?\"}]}",
                        {},
                        false};
            if (query.contains("post(post_id:$post_id)"))
                return {
                    200,
                    R"({"data":{"post":{"id":999,"post_id":123,"tags":"solo tagme","ext":"jpg","image_link":"/image/123"}}})",
                    {},
                    false};
            return {200, R"({"data":{}})", {}, false};
        };
        ShimmieProvider provider({"shm-recover", "test", ProviderKind::Shimmie, server.base(), "tester", true});
        std::optional<Result<AuthInfo>> auth;
        provider.authenticate("password", [&](auto result) { auth = result; });
        QTRY_VERIFY(auth.has_value());
        QVERIFY(*auth);
        QCOMPARE(schemaRequests, 1);
        std::optional<Result<Post>> post;
        provider.fetchPost(123, [&](auto result) { post = result; });
        QTRY_VERIFY(post.has_value());
        QVERIFY(*post);
        QCOMPARE(post->value->id, 123);
        QCOMPARE(schemaRequests, 2);
        QCOMPARE(server.requests.size(), 5);
        QVERIFY(server.requests.at(2).json().value("query").toString().contains("posts("));
        QVERIFY(server.requests.at(4).json().value("query").toString().contains("post(post_id:$post_id)"));
    }
    void networkErrorsAndIsolation()
    {
        HttpFixture one;
        HttpFixture two;
        one.handler = [](const FixtureRequest &) -> FixtureResponse {
            return {429, "{}", {{"Retry-After", "3"}}, false};
        };
        HttpClient a({"a", "one", ProviderKind::Danbooru, one.base(), "alice", true});
        HttpClient b({"b", "two", ProviderKind::Danbooru, two.base(), "bob", true});
        a.setBasicAuth("alice", "secret");
        std::optional<Result<HttpResponse>> result;
        a.send("GET", one.base("/profile.json"), {}, [&](auto response) { result = response; });
        QTRY_VERIFY(result.has_value());
        QVERIFY(!*result);
        QCOMPARE(result->error.kind, ErrorKind::RateLimit);
        QCOMPARE(result->error.retryAfterSeconds, 3);
        const auto before = one.requests.size();
        result.reset();
        a.send("GET", one.base("/posts.json"), {}, [&](auto response) { result = response; });
        QTRY_VERIFY(result.has_value());
        QCOMPARE(result->error.kind, ErrorKind::RateLimit);
        QCOMPARE(one.requests.size(), before);
        result.reset();
        b.send("GET", two.base("/posts.json"), {}, [&](auto response) { result = response; });
        QTRY_VERIFY(result.has_value());
        QVERIFY(*result);
        QVERIFY(!two.requests.last().headers.contains("authorization"));
        result.reset();
        a.send("GET", two.base("/posts.json"), {}, [&](auto response) { result = response; });
        QTRY_VERIFY(result.has_value());
        QCOMPARE(result->error.kind, ErrorKind::Permission);
        one.handler = [&two](const FixtureRequest &) -> FixtureResponse {
            return {302, "", {{"Location", two.base("/image").toEncoded()}}, false};
        };
        result.reset();
        a.send("GET", one.base("/image"), {}, [&](auto response) { result = response; }, true);
        QTRY_VERIFY(result.has_value());
        QVERIFY(*result);
        QVERIFY(!two.requests.last().headers.contains("authorization"));
        a.setSessionCookies("alice", "scoped-session");
        result.reset();
        a.send("GET", one.base("/image"), {}, [&](auto response) { result = response; }, true);
        QTRY_VERIFY(result.has_value());
        QVERIFY(*result);
        QVERIFY(!two.requests.last().headers.contains("cookie"));
    }
};

QTEST_GUILESS_MAIN(ProviderTests)
#include "test_providers.moc"
