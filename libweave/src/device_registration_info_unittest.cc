// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/device_registration_info.h"

#include <base/json/json_reader.h>
#include <base/json/json_writer.h>
#include <base/values.h>
#include <gtest/gtest.h>
#include <weave/provider/test/fake_task_runner.h>
#include <weave/provider/test/mock_config_store.h>
#include <weave/provider/test/mock_http_client.h>

#include "src/bind_lambda.h"
#include "src/commands/command_manager.h"
#include "src/commands/unittest_utils.h"
#include "src/http_constants.h"
#include "src/states/mock_state_change_queue_interface.h"
#include "src/states/state_manager.h"

using testing::_;
using testing::AtLeast;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::Mock;
using testing::Return;
using testing::ReturnRef;
using testing::ReturnRefOfCopy;
using testing::SaveArg;
using testing::StrictMock;
using testing::WithArgs;

namespace weave {

using test::CreateDictionaryValue;
using test::CreateValue;
using provider::test::MockHttpClient;
using provider::test::MockHttpClientResponse;
using provider::HttpClient;

namespace {

namespace test_data {

const char kServiceURL[] = "http://gcd.server.com/";
const char kOAuthURL[] = "http://oauth.server.com/";
const char kApiKey[] = "GOadRdTf9FERf0k4w6EFOof56fUJ3kFDdFL3d7f";
const char kClientId[] =
    "123543821385-sfjkjshdkjhfk234sdfsdfkskd"
    "fkjh7f.apps.googleusercontent.com";
const char kClientSecret[] = "5sdGdGlfolGlrFKfdFlgP6FG";
const char kDeviceId[] = "4a7ea2d1-b331-1e1f-b206-e863c7635196";
const char kClaimTicketId[] = "RTcUE";
const char kAccessToken[] =
    "ya29.1.AADtN_V-dLUM-sVZ0qVjG9Dxm5NgdS9J"
    "Mx_JLUqhC9bED_YFjzHZtYt65ZzXCS35NMAeaVZ"
    "Dei530-w0yE2urpQ";
const char kRefreshToken[] =
    "1/zQmxR6PKNvhcxf9SjXUrCjcmCrcqRKXctc6cp"
    "1nI-GQ";
const char kRobotAccountAuthCode[] =
    "4/Mf_ujEhPejVhOq-OxW9F5cSOnWzx."
    "YgciVjTYGscRshQV0ieZDAqiTIjMigI";
const char kRobotAccountEmail[] =
    "6ed0b3f54f9bd619b942f4ad2441c252@"
    "clouddevices.gserviceaccount.com";

}  // namespace test_data

std::string GetFormField(const std::string& data, const std::string& name) {
  EXPECT_FALSE(data.empty());
  for (const auto& i : WebParamsDecode(data)) {
    if (i.first == name)
      return i.second;
  }
  return {};
}

HttpClient::Response* ReplyWithJson(int status_code, const base::Value& json) {
  std::string text;
  base::JSONWriter::WriteWithOptions(
      json, base::JSONWriter::OPTIONS_PRETTY_PRINT, &text);

  MockHttpClientResponse* response = new StrictMock<MockHttpClientResponse>;
  EXPECT_CALL(*response, GetStatusCode())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(status_code));
  EXPECT_CALL(*response, GetContentType())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(http::kJsonUtf8));
  EXPECT_CALL(*response, GetData())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(text));
  return response;
}

std::pair<std::string, std::string> GetAuthHeader() {
  return {http::kAuthorization,
          std::string("Bearer ") + test_data::kAccessToken};
}

std::pair<std::string, std::string> GetJsonHeader() {
  return {http::kContentType, http::kJsonUtf8};
}

std::pair<std::string, std::string> GetFormHeader() {
  return {http::kContentType, http::kWwwFormUrlEncoded};
}

}  // anonymous namespace

class DeviceRegistrationInfoTest : public ::testing::Test {
 protected:
  void SetUp() override {
    EXPECT_CALL(mock_state_change_queue_, GetLastStateChangeId())
        .WillRepeatedly(Return(0));
    EXPECT_CALL(mock_state_change_queue_, MockAddOnStateUpdatedCallback(_))
        .WillRepeatedly(Return(nullptr));

    command_manager_ = std::make_shared<CommandManager>();
    state_manager_ = std::make_shared<StateManager>(&mock_state_change_queue_);

    std::unique_ptr<Config> config{new Config{&config_store_}};
    config_ = config.get();
    dev_reg_.reset(new DeviceRegistrationInfo{command_manager_, state_manager_,
                                              std::move(config), &task_runner_,
                                              &http_client_, nullptr});

    ReloadDefaults();
  }

  void ReloadDefaults() {
    EXPECT_CALL(config_store_, LoadDefaults(_))
        .WillOnce(Invoke([](Settings* settings) {
          settings->client_id = test_data::kClientId;
          settings->client_secret = test_data::kClientSecret;
          settings->api_key = test_data::kApiKey;
          settings->oem_name = "Coffee Pot Maker";
          settings->model_name = "Pot v1";
          settings->name = "Coffee Pot";
          settings->description = "Easy to clean";
          settings->location = "Kitchen";
          settings->local_anonymous_access_role = AuthScope::kViewer;
          settings->model_id = "AAAAA";
          settings->oauth_url = test_data::kOAuthURL;
          settings->service_url = test_data::kServiceURL;
          return true;
        }));
    config_->Load();
    dev_reg_->Start();
  }

  void ReloadSettings() {
    base::DictionaryValue dict;
    dict.SetString("refresh_token", test_data::kRefreshToken);
    dict.SetString("cloud_id", test_data::kDeviceId);
    dict.SetString("robot_account", test_data::kRobotAccountEmail);
    std::string json_string;
    base::JSONWriter::WriteWithOptions(
        dict, base::JSONWriter::OPTIONS_PRETTY_PRINT, &json_string);
    EXPECT_CALL(config_store_, LoadSettings()).WillOnce(Return(json_string));
    ReloadDefaults();
  }

  void PublishCommands(const base::ListValue& commands) {
    return dev_reg_->PublishCommands(commands);
  }

  bool RefreshAccessToken(ErrorPtr* error) const {
    bool succeeded = false;
    auto on_success = [&succeeded]() { succeeded = true; };
    auto on_failure = [&error](const Error* in_error) {
      if (error)
        *error = in_error->Clone();
    };
    dev_reg_->RefreshAccessToken(base::Bind(on_success),
                                 base::Bind(on_failure));
    return succeeded;
  }

  void SetAccessToken() { dev_reg_->access_token_ = test_data::kAccessToken; }

  GcdState GetGcdState() const { return dev_reg_->GetGcdState(); }

  provider::test::FakeTaskRunner task_runner_;
  provider::test::MockConfigStore config_store_;
  StrictMock<MockHttpClient> http_client_;
  base::DictionaryValue data_;
  Config* config_{nullptr};
  std::unique_ptr<DeviceRegistrationInfo> dev_reg_;
  std::shared_ptr<CommandManager> command_manager_;
  StrictMock<MockStateChangeQueueInterface> mock_state_change_queue_;
  std::shared_ptr<StateManager> state_manager_;
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(DeviceRegistrationInfoTest, GetServiceURL) {
  EXPECT_EQ(test_data::kServiceURL, dev_reg_->GetServiceURL());
  std::string url = test_data::kServiceURL;
  url += "registrationTickets";
  EXPECT_EQ(url, dev_reg_->GetServiceURL("registrationTickets"));
  url += "?key=";
  url += test_data::kApiKey;
  EXPECT_EQ(url, dev_reg_->GetServiceURL("registrationTickets",
                                         {{"key", test_data::kApiKey}}));
  url += "&restart=true";
  EXPECT_EQ(url, dev_reg_->GetServiceURL(
                     "registrationTickets",
                     {
                         {"key", test_data::kApiKey}, {"restart", "true"},
                     }));
}

TEST_F(DeviceRegistrationInfoTest, GetOAuthURL) {
  EXPECT_EQ(test_data::kOAuthURL, dev_reg_->GetOAuthURL());
  std::string url = test_data::kOAuthURL;
  url += "auth?scope=https%3A%2F%2Fwww.googleapis.com%2Fauth%2Fclouddevices&";
  url += "redirect_uri=urn%3Aietf%3Awg%3Aoauth%3A2.0%3Aoob&";
  url += "response_type=code&";
  url += "client_id=";
  url += test_data::kClientId;
  EXPECT_EQ(url, dev_reg_->GetOAuthURL(
                     "auth",
                     {{"scope", "https://www.googleapis.com/auth/clouddevices"},
                      {"redirect_uri", "urn:ietf:wg:oauth:2.0:oob"},
                      {"response_type", "code"},
                      {"client_id", test_data::kClientId}}));
}

TEST_F(DeviceRegistrationInfoTest, HaveRegistrationCredentials) {
  EXPECT_FALSE(dev_reg_->HaveRegistrationCredentials());
  ReloadSettings();

  EXPECT_CALL(http_client_,
              MockSendRequest(http::kPost, dev_reg_->GetOAuthURL("token"),
                              HttpClient::Headers{GetFormHeader()}, _, _))
      .WillOnce(WithArgs<3>(Invoke([](const std::string& data) {
        EXPECT_EQ("refresh_token", GetFormField(data, "grant_type"));
        EXPECT_EQ(test_data::kRefreshToken,
                  GetFormField(data, "refresh_token"));
        EXPECT_EQ(test_data::kClientId, GetFormField(data, "client_id"));
        EXPECT_EQ(test_data::kClientSecret,
                  GetFormField(data, "client_secret"));

        base::DictionaryValue json;
        json.SetString("access_token", test_data::kAccessToken);
        json.SetInteger("expires_in", 3600);

        return ReplyWithJson(200, json);
      })));

  EXPECT_TRUE(RefreshAccessToken(nullptr));
  EXPECT_TRUE(dev_reg_->HaveRegistrationCredentials());
}

TEST_F(DeviceRegistrationInfoTest, CheckAuthenticationFailure) {
  ReloadSettings();
  EXPECT_EQ(GcdState::kConnecting, GetGcdState());

  EXPECT_CALL(http_client_,
              MockSendRequest(http::kPost, dev_reg_->GetOAuthURL("token"),
                              HttpClient::Headers{GetFormHeader()}, _, _))
      .WillOnce(WithArgs<3>(Invoke([](const std::string& data) {
        EXPECT_EQ("refresh_token", GetFormField(data, "grant_type"));
        EXPECT_EQ(test_data::kRefreshToken,
                  GetFormField(data, "refresh_token"));
        EXPECT_EQ(test_data::kClientId, GetFormField(data, "client_id"));
        EXPECT_EQ(test_data::kClientSecret,
                  GetFormField(data, "client_secret"));

        base::DictionaryValue json;
        json.SetString("error", "unable_to_authenticate");
        return ReplyWithJson(400, json);
      })));

  ErrorPtr error;
  EXPECT_FALSE(RefreshAccessToken(&error));
  EXPECT_TRUE(error->HasError(kErrorDomainOAuth2, "unable_to_authenticate"));
  EXPECT_EQ(GcdState::kConnecting, GetGcdState());
}

TEST_F(DeviceRegistrationInfoTest, CheckDeregistration) {
  ReloadSettings();
  EXPECT_EQ(GcdState::kConnecting, GetGcdState());

  EXPECT_CALL(http_client_,
              MockSendRequest(http::kPost, dev_reg_->GetOAuthURL("token"),
                              HttpClient::Headers{GetFormHeader()}, _, _))
      .WillOnce(WithArgs<3>(Invoke([](const std::string& data) {
        EXPECT_EQ("refresh_token", GetFormField(data, "grant_type"));
        EXPECT_EQ(test_data::kRefreshToken,
                  GetFormField(data, "refresh_token"));
        EXPECT_EQ(test_data::kClientId, GetFormField(data, "client_id"));
        EXPECT_EQ(test_data::kClientSecret,
                  GetFormField(data, "client_secret"));

        base::DictionaryValue json;
        json.SetString("error", "invalid_grant");
        return ReplyWithJson(400, json);
      })));

  ErrorPtr error;
  EXPECT_FALSE(RefreshAccessToken(&error));
  EXPECT_TRUE(error->HasError(kErrorDomainOAuth2, "invalid_grant"));
  EXPECT_EQ(GcdState::kInvalidCredentials, GetGcdState());
}

TEST_F(DeviceRegistrationInfoTest, GetDeviceInfo) {
  ReloadSettings();
  SetAccessToken();

  EXPECT_CALL(http_client_,
              MockSendRequest(
                  http::kGet, dev_reg_->GetDeviceURL(),
                  HttpClient::Headers{GetAuthHeader(), GetJsonHeader()}, _, _))
      .WillOnce(WithArgs<3>(Invoke([](const std::string& data) {
        base::DictionaryValue json;
        json.SetString("channel.supportedType", "xmpp");
        json.SetString("deviceKind", "vendor");
        json.SetString("id", test_data::kDeviceId);
        json.SetString("kind", "clouddevices#device");
        return ReplyWithJson(200, json);
      })));

  bool succeeded = false;
  auto on_success = [&succeeded, this](const base::DictionaryValue& info) {
    std::string id;
    EXPECT_TRUE(info.GetString("id", &id));
    EXPECT_EQ(test_data::kDeviceId, id);
    succeeded = true;
  };
  auto on_failure = [](const Error* error) {
    FAIL() << "Should not be called";
  };
  dev_reg_->GetDeviceInfo(base::Bind(on_success), base::Bind(on_failure));
  EXPECT_TRUE(succeeded);
}

TEST_F(DeviceRegistrationInfoTest, RegisterDevice) {
  auto json_base = CreateDictionaryValue(R"({
    'base': {
      'reboot': {
        'parameters': {'delay': 'integer'},
        'minimalRole': 'user',
        'results': {}
      },
      'shutdown': {
        'parameters': {},
        'minimalRole': 'user',
        'results': {}
      }
    }
  })");
  EXPECT_TRUE(command_manager_->LoadBaseCommands(*json_base, nullptr));
  auto json_cmds = CreateDictionaryValue(R"({
    'base': {
      'reboot': {
        'parameters': {'delay': {'minimum': 10}},
        'minimalRole': 'user',
        'results': {}
      }
    },
    'robot': {
      '_jump': {
        'parameters': {'_height': 'integer'},
        'minimalRole': 'user',
        'results': {}
      }
    }
  })");
  EXPECT_TRUE(command_manager_->LoadCommands(*json_cmds, nullptr));

  std::string ticket_url = dev_reg_->GetServiceURL("registrationTickets/") +
                           test_data::kClaimTicketId;
  EXPECT_CALL(
      http_client_,
      MockSendRequest(http::kPatch, ticket_url + "?key=" + test_data::kApiKey,
                      HttpClient::Headers{GetJsonHeader()}, _, _))
      .WillOnce(WithArgs<3>(Invoke([](const std::string& data) {
        auto json = test::CreateDictionaryValue(data);
        EXPECT_NE(nullptr, json.get());
        std::string value;
        EXPECT_TRUE(json->GetString("id", &value));
        EXPECT_EQ(test_data::kClaimTicketId, value);
        EXPECT_TRUE(
            json->GetString("deviceDraft.channel.supportedType", &value));
        EXPECT_EQ("pull", value);
        EXPECT_TRUE(json->GetString("oauthClientId", &value));
        EXPECT_EQ(test_data::kClientId, value);
        EXPECT_TRUE(json->GetString("deviceDraft.description", &value));
        EXPECT_EQ("Easy to clean", value);
        EXPECT_TRUE(json->GetString("deviceDraft.location", &value));
        EXPECT_EQ("Kitchen", value);
        EXPECT_TRUE(json->GetString("deviceDraft.modelManifestId", &value));
        EXPECT_EQ("AAAAA", value);
        EXPECT_TRUE(json->GetString("deviceDraft.name", &value));
        EXPECT_EQ("Coffee Pot", value);
        base::DictionaryValue* commandDefs = nullptr;
        EXPECT_TRUE(
            json->GetDictionary("deviceDraft.commandDefs", &commandDefs));
        EXPECT_FALSE(commandDefs->empty());

        auto expected = R"({
            'base': {
              'reboot': {
                'parameters': {
                  'delay': {
                    'minimum': 10,
                    'type': 'integer'
                  }
                },
                'minimalRole': 'user'
              }
            },
            'robot': {
              '_jump': {
                'parameters': {
                  '_height': {
                    'type': 'integer'
                  }
                },
                'minimalRole': 'user'
              }
            }
          })";
        EXPECT_JSON_EQ(expected, *commandDefs);

        base::DictionaryValue json_resp;
        json_resp.SetString("id", test_data::kClaimTicketId);
        json_resp.SetString("kind", "clouddevices#registrationTicket");
        json_resp.SetString("oauthClientId", test_data::kClientId);
        base::DictionaryValue* device_draft = nullptr;
        EXPECT_TRUE(json->GetDictionary("deviceDraft", &device_draft));
        device_draft = device_draft->DeepCopy();
        device_draft->SetString("id", test_data::kDeviceId);
        device_draft->SetString("kind", "clouddevices#device");
        json_resp.Set("deviceDraft", device_draft);

        return ReplyWithJson(200, json_resp);
      })));

  EXPECT_CALL(http_client_,
              MockSendRequest(http::kPost, ticket_url + "/finalize?key=" +
                                               test_data::kApiKey,
                              HttpClient::Headers{}, _, _))
      .WillOnce(InvokeWithoutArgs([]() {
        base::DictionaryValue json;
        json.SetString("id", test_data::kClaimTicketId);
        json.SetString("kind", "clouddevices#registrationTicket");
        json.SetString("oauthClientId", test_data::kClientId);
        json.SetString("userEmail", "user@email.com");
        json.SetString("deviceDraft.id", test_data::kDeviceId);
        json.SetString("deviceDraft.kind", "clouddevices#device");
        json.SetString("deviceDraft.channel.supportedType", "xmpp");
        json.SetString("robotAccountEmail", test_data::kRobotAccountEmail);
        json.SetString("robotAccountAuthorizationCode",
                       test_data::kRobotAccountAuthCode);
        return ReplyWithJson(200, json);
      }));

  EXPECT_CALL(http_client_,
              MockSendRequest(http::kPost, dev_reg_->GetOAuthURL("token"),
                              HttpClient::Headers{GetFormHeader()}, _, _))
      .WillOnce(WithArgs<3>(Invoke([](const std::string& data) {
        EXPECT_EQ("authorization_code", GetFormField(data, "grant_type"));
        EXPECT_EQ(test_data::kRobotAccountAuthCode, GetFormField(data, "code"));
        EXPECT_EQ(test_data::kClientId, GetFormField(data, "client_id"));
        EXPECT_EQ(test_data::kClientSecret,
                  GetFormField(data, "client_secret"));

        EXPECT_EQ("oob", GetFormField(data, "redirect_uri"));
        EXPECT_EQ("https://www.googleapis.com/auth/clouddevices",
                  GetFormField(data, "scope"));

        base::DictionaryValue json;
        json.SetString("access_token", test_data::kAccessToken);
        json.SetString("token_type", "Bearer");
        json.SetString("refresh_token", test_data::kRefreshToken);
        json.SetInteger("expires_in", 3600);

        return ReplyWithJson(200, json);
      })));

  bool done = false;
  dev_reg_->RegisterDevice(
      test_data::kClaimTicketId, base::Bind([this, &done]() {
        done = true;
        task_runner_.Break();
        EXPECT_EQ(GcdState::kConnecting, GetGcdState());

        // Validate the device info saved to storage...
        EXPECT_EQ(test_data::kDeviceId, dev_reg_->GetSettings().cloud_id);
        EXPECT_EQ(test_data::kRefreshToken,
                  dev_reg_->GetSettings().refresh_token);
        EXPECT_EQ(test_data::kRobotAccountEmail,
                  dev_reg_->GetSettings().robot_account);
      }),
      base::Bind([](const Error* error) { ADD_FAILURE(); }));
  task_runner_.Run();
  EXPECT_TRUE(done);
}

TEST_F(DeviceRegistrationInfoTest, OOBRegistrationStatus) {
  // After we've been initialized, we should be either offline or
  // unregistered, depending on whether or not we've found credentials.
  EXPECT_EQ(GcdState::kUnconfigured, GetGcdState());
  // Put some credentials into our state, make sure we call that offline.
  ReloadSettings();
  EXPECT_EQ(GcdState::kConnecting, GetGcdState());
}

class DeviceRegistrationInfoUpdateCommandTest
    : public DeviceRegistrationInfoTest {
 protected:
  void SetUp() override {
    DeviceRegistrationInfoTest::SetUp();

    ReloadSettings();
    SetAccessToken();

    auto json_cmds = CreateDictionaryValue(R"({
      'robot': {
        '_jump': {
          'parameters': {'_height': 'integer'},
          'progress': {'progress': 'integer'},
          'results': {'status': 'string'},
          'minimalRole': 'user'
        }
      }
    })");
    EXPECT_TRUE(command_manager_->LoadCommands(*json_cmds, nullptr));

    command_url_ = dev_reg_->GetServiceURL("commands/1234");

    auto commands_json = CreateValue(R"([{
      'name':'robot._jump',
      'id':'1234',
      'parameters': {'_height': 100},
      'minimalRole': 'user'
    }])");
    ASSERT_NE(nullptr, commands_json.get());
    const base::ListValue* command_list = nullptr;
    ASSERT_TRUE(commands_json->GetAsList(&command_list));
    PublishCommands(*command_list);
    command_ = command_manager_->FindCommand("1234");
    ASSERT_NE(nullptr, command_);
  }

  void TearDown() override {
    task_runner_.RunOnce();
    DeviceRegistrationInfoTest::TearDown();
  }

  Command* command_{nullptr};
  std::string command_url_;
};

TEST_F(DeviceRegistrationInfoUpdateCommandTest, SetProgress) {
  EXPECT_CALL(http_client_,
              MockSendRequest(
                  http::kPatch, command_url_,
                  HttpClient::Headers{GetAuthHeader(), GetJsonHeader()}, _, _))
      .WillOnce(WithArgs<3>(Invoke([](const std::string& data) {
        EXPECT_JSON_EQ(R"({"state":"inProgress", "progress":{"progress":18}})",
                       *CreateDictionaryValue(data));
        base::DictionaryValue json;
        return ReplyWithJson(200, json);
      })));
  EXPECT_TRUE(command_->SetProgress(*CreateDictionaryValue("{'progress':18}"),
                                    nullptr));
}

TEST_F(DeviceRegistrationInfoUpdateCommandTest, Complete) {
  EXPECT_CALL(http_client_,
              MockSendRequest(
                  http::kPatch, command_url_,
                  HttpClient::Headers{GetAuthHeader(), GetJsonHeader()}, _, _))
      .WillOnce(WithArgs<3>(Invoke([](const std::string& data) {
        EXPECT_JSON_EQ(R"({"state":"done", "results":{"status":"Ok"}})",
                       *CreateDictionaryValue(data));
        base::DictionaryValue json;
        return ReplyWithJson(200, json);
      })));
  EXPECT_TRUE(
      command_->Complete(*CreateDictionaryValue("{'status': 'Ok'}"), nullptr));
}

TEST_F(DeviceRegistrationInfoUpdateCommandTest, Cancel) {
  EXPECT_CALL(http_client_,
              MockSendRequest(
                  http::kPatch, command_url_,
                  HttpClient::Headers{GetAuthHeader(), GetJsonHeader()}, _, _))
      .WillOnce(WithArgs<3>(Invoke([](const std::string& data) {
        EXPECT_JSON_EQ(R"({"state":"cancelled"})",
                       *CreateDictionaryValue(data));
        base::DictionaryValue json;
        return ReplyWithJson(200, json);
      })));
  EXPECT_TRUE(command_->Cancel(nullptr));
}

}  // namespace weave
