#include "playfab.hpp"
#include "scores.hpp"
#include "files.hpp"
#include "mod_tools.hpp"
#ifdef STEAMWORKS
#include "steam.hpp"
#endif

#ifdef USE_PLAYFAB

#ifdef WINDOWS
#define STRINGIZE2(s) #s
#define STRINGIZE(s) STRINGIZE2(s)
#define BUILD_ENV_PFTID STRINGIZE(BUILD_PFTID)
#define BUILD_ENV_PFHID STRINGIZE(BUILD_PFHID)
#endif

PlayfabUser_t playfabUser;
void PlayfabUser_t::OnLoginSuccess(const PlayFab::ClientModels::LoginResult& result, void* customData)
{
    logInfo("Logged in successfully");
    playfabUser.loggingIn = false;
    playfabUser.bLoggedIn = true;
    playfabUser.errorLogin = false;
    playfabUser.currentUser = result.PlayFabId;
    playfabUser.authenticationRefresh = TICKS_PER_SECOND * 60 * 60 * 8;

    PlayFab::ClientModels::UpdateUserTitleDisplayNameRequest request;
    request.DisplayName = "";
    switch ( playfabUser.type )
    {
    case PlayerType_Steam:
#ifdef STEAMWORKS
        if ( SteamUser()->BLoggedOn() )
        {
            request.DisplayName = SteamFriends()->GetPersonaName();
            request.DisplayName = request.DisplayName.substr(0, 25);
        }
        SteamClientConsumeAuthTicket();
#endif
        break;
    case PlayerType_Epic:
        if ( EOS.CurrentUserInfo.bUserLoggedIn )
        {
            if ( !EOS.CurrentUserInfo.bUserInfoRequireUpdate )
            {
                request.DisplayName = EOS.CurrentUserInfo.Name;
                request.DisplayName = request.DisplayName.substr(0, 25);
            }
        }
        break;
    default:
        break;
    }
    if ( request.DisplayName != "" )
    {
        PlayFab::PlayFabClientAPI::UpdateUserTitleDisplayName(request, OnDisplayNameUpdateSuccess);
    }
}

void PlayfabUser_t::OnLoginFail(const PlayFab::PlayFabError& error, void* customData)
{
    logError("Failed to login: %s", error.ErrorMessage.c_str());
    playfabUser.loggingIn = false;
    playfabUser.bLoggedIn = false;
    playfabUser.errorLogin = true;
    playfabUser.authenticationRefresh = TICKS_PER_SECOND * 60;
}

void PlayfabUser_t::OnDisplayNameUpdateSuccess(const PlayFab::ClientModels::UpdateUserTitleDisplayNameResult& result, void* customData)
{
    playfabUser.currentUserName = result.DisplayName;
    logInfo("Updated display name: %s", result.DisplayName.c_str());
}

void PlayfabUser_t::loginEpic()
{
    PlayFab::ClientModels::LoginWithOpenIdConnectRequest request;
    request.CreateAccount = true;
    request.ConnectionId = "openid_epic";
    request.IdToken = EOS.getAuthToken();
    if ( EOS.getAuthToken() == "" )
    {
        playfabUser.authenticationRefresh = TICKS_PER_SECOND * 5;
        return;
    }
    playfabUser.loggingIn = true;
    PlayFab::PlayFabClientAPI::LoginWithOpenIdConnect(request, OnLoginSuccess, OnLoginFail);
}

void PlayfabUser_t::loginSteam()
{
#ifdef STEAMWORKS
    PlayFab::ClientModels::LoginWithSteamRequest request;
    request.CreateAccount = true;
    request.SteamTicket = SteamClientRequestAuthTicket();
    playfabUser.loggingIn = true;
    PlayFab::PlayFabClientAPI::LoginWithSteam(request, OnLoginSuccess, OnLoginFail);
#endif
}

void PlayfabUser_t::OnCloudScriptExecute(const PlayFab::ClientModels::ExecuteCloudScriptResult& result, void* customData)
{
    printlog("%d", result.ProcessorTimeSeconds);
}

void PlayfabUser_t::OnCloudScriptFailure(const PlayFab::PlayFabError& error, void* customData)
{
    logError("Update failure: %s", error.ErrorMessage.c_str());
}

void jsonValueToInt(Json::Value& val, std::string key, int& set)
{
    if ( val.isMember(key) )
    {
        if ( val[key].isInt() )
        {
            set = val[key].asInt();
        }
    }
}

void jsonValueToInt(Json::Value& val, std::string key, Uint32& set)
{
    if ( val.isMember(key) )
    {
        if ( val[key].isInt() )
        {
            set = val[key].asInt();
        }
    }
}

void jsonValueToString(Json::Value& val, std::string key, std::string& set)
{
    if ( val.isMember(key) )
    {
        if ( val[key].isString() )
        {
            set = val[key].asString();
        }
    }
}

void jsonArrayToInt(Json::Value& val, Json::ArrayIndex index, int& set)
{
    if ( val.isArray() )
    {
        if ( index < val.size() )
        {
            if ( val[index].isInt() )
            {
                set = val[index].asInt();
            }
        }
    }
}

void jsonValueToItem(Json::Value& val, std::string key, SaveGameInfo::Player::stat_t::item_t& set)
{
    if ( val.isMember(key) )
    {
        if ( val[key].isArray() && val[key].size() >= 6 )
        {
            for ( int i = 0; i < 6; ++i )
            {
                if ( val[key][i].isInt() )
                {
                    switch ( i )
                    {
                    case 0:
                        set.type = (Uint32)val[key][i].asInt();
                        break;
                    case 1:
                        set.status = (Uint32)val[key][i].asInt();
                        break;
                    case 2:
                        set.beatitude = (Uint32)val[key][i].asInt();
                        break;
                    case 3:
                        set.count = (Uint32)val[key][i].asInt();
                        break;
                    case 4:
                        set.appearance = (Uint32)val[key][i].asInt();
                        break;
                    case 5:
                        set.identified = val[key][i].asInt() > 0 ? true : false;
                        break;
                    }
                }
            }
        }
    }
}

int parseOnlineHiscore(SaveGameInfo& info, Json::Value score)
{
    info.players.resize(1);
    info.players_connected.resize(MAXPLAYERS);
    info.players_connected[0] = 1;
    auto& player = info.players[0];

    auto t = getTime();
    struct tm* tm = localtime(&t); assert(tm);

    char buf[64];
    info.magic_cookie = "BARONYJSONSAVE";
    info.game_version = getSavegameVersion(VERSION);
    info.timestamp = getTimeAndDateFormatted(t, buf, sizeof(buf));
    info.gamekey = local_rng.rand();
    info.hiscore_dummy_loading = true;
    if ( info.gamekey == 0 ) { ++info.gamekey; }

    for ( int i = 0; i < NUM_HOTBAR_SLOTS; ++i ) {
        player.hotbar[i] = UINT32_MAX;
        for ( int j = 0; j < NUM_HOTBAR_ALTERNATES; ++j ) {
            player.hotbar_alternate[j][i] = UINT32_MAX;
        }
    }

    // spells
    player.selected_spell = UINT32_MAX;
    player.selected_spell_last_appearance = -1;
    for ( int i = 0; i < NUM_HOTBAR_ALTERNATES; ++i )
    {
        player.selected_spell_alternate[i] = UINT32_MAX;
    }

    for ( auto& m : score.getMemberNames() )
    {
        if ( m == "dispname" )
        {
            jsonValueToString(score, m, info.gamename);
        }
        else if ( m == "rank" )
        {
            jsonValueToInt(score, m, info.hiscore_rank);
        }
        else if ( m == "stats" )
        {
            for ( auto& s : score[m].getMemberNames() )
            {
                if ( s == "STR" )
                {
                    jsonValueToInt(score[m], s, player.stats.STR);
                }
                else if ( s == "DEX" )
                {
                    jsonValueToInt(score[m], s, player.stats.DEX);
                }
                else if ( s == "CON" )
                {
                    jsonValueToInt(score[m], s, player.stats.CON);
                }
                else if ( s == "INT" )
                {
                    jsonValueToInt(score[m], s, player.stats.INT);
                }
                else if ( s == "PER" )
                {
                    jsonValueToInt(score[m], s, player.stats.PER);
                }
                else if ( s == "CHR" )
                {
                    jsonValueToInt(score[m], s, player.stats.CHR);
                }
                else if ( s == "MAXHP" )
                {
                    jsonValueToInt(score[m], s, player.stats.maxHP);
                }
                else if ( s == "MAXMP" )
                {
                    jsonValueToInt(score[m], s, player.stats.maxMP);
                }
                else if ( s == "GOLD" )
                {
                    jsonValueToInt(score[m], s, player.stats.GOLD);
                }
                else if ( s == "LVL" )
                {
                    jsonValueToInt(score[m], s, player.stats.LVL);
                }
                else if ( s == "class" )
                {
                    jsonValueToInt(score[m], s, player.char_class);
                }
                else if ( s == "appearance" )
                {
                    jsonValueToInt(score[m], s, player.stats.appearance);
                }
                else if ( s == "race" )
                {
                    jsonValueToInt(score[m], s, player.race);
                }
                else if ( s == "name" )
                {
                    jsonValueToString(score[m], s, player.stats.name);
                }
                else if ( s == "sex" )
                {
                    jsonValueToInt(score[m], s, player.stats.sex);
                }
                else if ( s == "kill_by" )
                {
                    jsonValueToInt(score[m], s, info.hiscore_killed_by);
                }
                else if ( s == "kill_item" )
                {
                    jsonValueToInt(score[m], s, info.hiscore_killed_item);
                }
                else if ( s == "kill_mon" )
                {
                    jsonValueToInt(score[m], s, info.hiscore_killed_monster);
                }
            }
        }
        else if ( m == "helmet" 
            || m == "breastplate"
            || m == "gloves" 
            || m == "shoes" 
            || m == "shield" 
            || m == "weapon" 
            || m == "cloak" 
            || m == "amulet" 
            || m == "ring" 
            || m == "mask" )
        {
            size_t slotIndex = player.stats.inventory.size();
            if ( score[m].isArray() && score[m].size() == 0 )
            {
                slotIndex = UINT32_MAX;
            }
            else
            {
                player.stats.inventory.push_back(SaveGameInfo::Player::stat_t::item_t());
                auto& item = player.stats.inventory.back();
                jsonValueToItem(score, m, item);
            }
            player.stats.player_equipment.push_back(std::make_pair(m, slotIndex));
        }
        else if ( m == "victory" )
        {
            jsonValueToInt(score, m, info.hiscore_victory);
        }
        else if ( m == "lvl" )
        {
            jsonValueToInt(score, m, info.dungeon_lvl);
        }
        else if ( m == "multiplayer" )
        {
            jsonValueToInt(score, m, info.multiplayer_type);
        }
        else if ( m == "secret" )
        {
            jsonValueToInt(score, m, info.level_track);
        }
        else if ( m == "time" )
        {
            jsonValueToInt(score, m, info.gametimer);
        }
        else if ( m == "totalscore" )
        {
            jsonValueToInt(score, m, info.hiscore_totalscore);
        }
        else if ( m == "flags" )
        {
            jsonValueToInt(score, m, info.svflags);
        }
        else if ( m == "attributes" )
        {
            for ( auto& s : score[m].getMemberNames() )
            {
                if ( s == "kills" )
                {
                    player.kills.resize(NUMMONSTERS);
                    for ( Json::ArrayIndex i = 0; i < score[m][s].size() && i < NUMMONSTERS; ++i )
                    {
                        jsonArrayToInt(score[m][s], i, player.kills[i]);
                    }
                }
                else if ( s == "proficiencies" )
                {
                    player.stats.PROFICIENCIES.resize(NUMPROFICIENCIES);
                    for ( Json::ArrayIndex i = 0; i < score[m][s].size() && i < NUMPROFICIENCIES; ++i )
                    {
                        jsonArrayToInt(score[m][s], i, player.stats.PROFICIENCIES[i]);
                    }
                }
                else if ( s == "conducts" )
                {
                    for ( Json::ArrayIndex i = 0; i < score[m][s].size() && i < (NUM_CONDUCT_CHALLENGES + 4); ++i )
                    {
                        if ( i < 4 )
                        {
                            int tmp = 0;
                            jsonArrayToInt(score[m][s], i, tmp);
                            switch ( i )
                            {
                            case 0:
                                player.conductPenniless = tmp;
                                break;
                            case 1:
                                player.conductFoodless = tmp;
                                break;
                            case 2:
                                player.conductVegetarian = tmp;
                                break;
                            case 3:
                                player.conductIlliterate = tmp;
                                break;
                            default:
                                break;
                            }
                        }
                        else
                        {
                            jsonArrayToInt(score[m][s], i, player.additionalConducts[i - 4]);
                        }
                    }
                }
            }
        }
    }
    if ( player.race > 0 && player.stats.appearance != 0 )
    {
        player.race = RACE_HUMAN; // set to human appearance for aesthetic scores
    }
    player.additionalConducts[CONDUCT_MODDED] = 0; // don't display this on the leaderboard window
    return 0;
}

void PlayfabUser_t::OnFunctionExecute(const PlayFab::CloudScriptModels::ExecuteFunctionResult& result, void* customData)
{
    PlayFab::PlayFabErrorCode code = PlayFab::PlayFabErrorCode::PlayFabErrorUnknownError;
    int retry = 0;
    std::string message = "";
    Json::Value data;
    for ( auto& m : result.FunctionResult.getMemberNames() )
    {
        if ( result.FunctionResult[m].isString() )
        {
            if ( m == "message" )
            {
                message = result.FunctionResult[m].asCString();
            }
            else if ( m == "data" )
            {
                data = result.FunctionResult[m].asCString();
            }
        }
        else if ( result.FunctionResult[m].isInt() )
        {
            if ( m == "code" )
            {
                code = (PlayFab::PlayFabErrorCode)result.FunctionResult[m].asInt();
            }
            else if ( m == "retry" )
            {
                retry = result.FunctionResult[m].asInt();
            }
        }
        else
        {
            if ( m == "data" )
            {
                data = result.FunctionResult[m];
            }
        }
    }

    if ( (int)code == 200 || code == PlayFab::PlayFabErrorCode::PlayFabErrorSuccess )
    {
        code = PlayFab::PlayFabErrorCode::PlayFabErrorSuccess;
        logInfo("\"%s\": [%d] [%s] (%dms)",
            result.FunctionName.c_str(), (int)code, message.c_str(), result.ExecutionTimeMilliseconds);
    }
    else
    {
        logError("\"%s\": [%d] [%s] (%dms)",
            result.FunctionName.c_str(), (int)code, message.c_str(), result.ExecutionTimeMilliseconds);
    }

    if ( result.FunctionName == "PostScore" )
    {
        int sequence = reinterpret_cast<intptr_t>(customData);
        for ( auto it = playfabUser.postScoreHandler.queue.begin();
            it != playfabUser.postScoreHandler.queue.end(); ++it )
        {
            if ( (*it).sequence == sequence )
            {
                (*it).inprogress = false;
                (*it).code = code;
                Uint32 retryTicks = (retry > 0 ? (ticks + TICKS_PER_SECOND * retry) : 0);
                (*it).retryTicks = retryTicks;
                if ( code == PlayFab::PlayFabErrorCode::PlayFabErrorSuccess
                    || ((int)code >= 400 && (int)code < 500 && (*it).retryTicks == 0) )
                {
                    playfabUser.postScoreHandler.queue.erase(it);
                }
                else
                {
                    if ( retryTicks == 0 ) // unknown error, wait some time
                    {
                        (*it).retryTicks = TICKS_PER_SECOND * 60;
                    }
                }
                break;
            }
        }
    }
    else if ( result.FunctionName == "LeaderboardGetPlayerData" )
    {
        int sequence = reinterpret_cast<intptr_t>(customData);
        if ( result.Request["FunctionParameter"].isMember("players")
            && result.Request["FunctionParameter"].isMember("lid") )
        {
            std::string lid = result.Request["FunctionParameter"]["lid"].asString();
            auto& leaderboard = playfabUser.leaderboardData.leaderboards[lid];
            std::set<std::string> recv_ids;
            leaderboard.name = lid;
            if ( code == PlayFab::PlayFabErrorCode::PlayFabErrorSuccess )
            {
                if ( leaderboard.awaitingResponse.find(sequence) != leaderboard.awaitingResponse.end() )
                {
                    leaderboard.awaitingResponse.erase(sequence);
                    if ( intro )
                    {
                        Json::Value v;
                        Json::Reader reader;
                        if ( data.type() == Json::stringValue )
                        {
                            if ( reader.parse(data.asCString(), v) )
                            {
                                for ( auto& key : v.getMemberNames() )
                                {
                                    auto& vkey = v[key];
                                    if ( vkey.type() == Json::stringValue )
                                    {
                                        recv_ids.insert(key);
                                        int hiscore_loadstatus = leaderboard.playerData[key].hiscore_loadstatus;
                                        leaderboard.playerData[key] = SaveGameInfo();
                                        leaderboard.playerData[key].hiscore_dummy_loading = true;
                                        leaderboard.playerData[key].hiscore_loadstatus = hiscore_loadstatus;
                                        Json::Value v2;
                                        if ( reader.parse(vkey.asCString(), v2) )
                                        {
                                            parseOnlineHiscore(leaderboard.playerData[key], v2);
                                        }
                                    }
                                }
                            }
                        }
                    }
                    if ( leaderboard.awaitingResponse.empty() )
                    {
                        leaderboard.loading = false;
                        if ( intro )
                        {
                            for ( auto& entry : leaderboard.displayedRanks )
                            {
                                auto& info = leaderboard.playerData[entry.id];
                                info.hiscore_dummy_loading = true;
                                if ( recv_ids.find(entry.id) != recv_ids.end() )
                                {
                                    entry.hasData = true;
                                }
                                entry.awaitingData = false;
                                entry.rank = info.hiscore_rank;
                                entry.score = info.hiscore_totalscore;
                            }
                        }
                    }
                }
            }
        }
    }
    else if ( result.FunctionName == "LeaderboardGetTop100" )
    {
        int sequence = reinterpret_cast<intptr_t>(customData);
        if ( result.Request["FunctionParameter"].isMember("lid") )
        {
            std::string lid = result.Request["FunctionParameter"]["lid"].asString();
            auto& leaderboard = playfabUser.leaderboardData.leaderboards[lid];
            leaderboard.name = lid;
            if ( code == PlayFab::PlayFabErrorCode::PlayFabErrorSuccess )
            {
                if ( leaderboard.awaitingResponse.find(sequence) != leaderboard.awaitingResponse.end() )
                {
                    leaderboard.awaitingResponse.erase(sequence);
                    if ( intro )
                    {
                        for ( auto itr = data.begin(); itr != data.end(); ++itr)
                        {
                            if ( itr->type() == Json::ValueType::stringValue )
                            {
                                Json::Value v;
                                Json::Reader reader;
                                if ( reader.parse(itr->asCString(), v) )
                                {
                                    for ( auto& key : v.getMemberNames() )
                                    {
                                        leaderboard.playerData[key] = SaveGameInfo();
                                        parseOnlineHiscore(leaderboard.playerData[key], v[key]);
                                        leaderboard.sortedData.push(std::make_pair(-leaderboard.playerData[key].hiscore_rank, key));
                                    }
                                }
                            }
                        }
                    }
                }
                if ( leaderboard.awaitingResponse.empty() )
                {
                    leaderboard.loading = false;
                    while ( !leaderboard.sortedData.empty() )
                    {
                        if ( intro )
                        {
                            leaderboard.displayedRanks.push_back(LeaderboardData_t::LeaderBoard_t::Entry_t());
                            auto& entry = leaderboard.displayedRanks.back();
                            entry.id = leaderboard.sortedData.top().second;

                            auto& info = leaderboard.playerData[entry.id];
                            info.hiscore_dummy_loading = true;
                            entry.displayName = info.gamename;
                            entry.hasData = true;
                            entry.rank = info.hiscore_rank;
                            entry.score = info.hiscore_totalscore;
                        }
                        leaderboard.sortedData.pop();
                    }
                }
            }
            else
            {
                if ( leaderboard.awaitingResponse.find(sequence) != leaderboard.awaitingResponse.end() )
                {
                    leaderboard.awaitingResponse[sequence].second = code;
                }
            }
        }
    }
}

int PlayfabUser_t::PostScoreHandler_t::sequenceIDs = 0;
Uint32 PlayfabUser_t::PostScoreHandler_t::lastPostTicks = 0;
int PlayfabUser_t::LeaderboardData_t::sequenceIDs = 0;

void PlayfabUser_t::getLeaderboardTop100(std::string lid)
{
    if ( !bLoggedIn )
    {
        return;
    }

    if ( leaderboardData.leaderboards[lid].loading )
    {
        for ( auto pair : leaderboardData.leaderboards[lid].awaitingResponse )
        {
            if ( (ticks - pair.second.first) < 5 * TICKS_PER_SECOND )
            {
                return; // waiting on a leaderboard to load in
            }
        }
    }

   leaderboardData.leaderboards.erase(lid);
    getLeaderboardTop100Data(lid, 0, 50);
    getLeaderboardTop100Data(lid, 50, 50);
}

void PlayfabUser_t::getLeaderboardTop100Data(std::string lid, int start, int numEntries)
{
    if ( !bLoggedIn )
    {
        return;
    }

    PlayFab::CloudScriptModels::ExecuteFunctionRequest request;
    request.FunctionName = "LeaderboardGetTop100";
    request.GeneratePlayStreamEvent = false;
    request.CustomTags["default"] = 1;
    request.CustomTags["lid"] = lid;

    Json::Value v;
    v["lid"] = lid;
    v["start"] = start;
    v["end"] = std::max(start, start + (numEntries - 1));
    request.FunctionParameter = v;

    playfabUser.leaderboardData.leaderboards[lid].awaitingResponse[LeaderboardData_t::sequenceIDs] = 
        std::make_pair(ticks, PlayFab::PlayFabErrorCode::PlayFabErrorUnknownError);

    PlayFab::PlayFabCloudScriptAPI::ExecuteFunction(request, OnFunctionExecute, OnCloudScriptFailure,
        (void*)(intptr_t)(LeaderboardData_t::sequenceIDs));
    ++LeaderboardData_t::sequenceIDs;
}

void PlayfabUser_t::PostScoreHandler_t::ScoreUpdate_t::post()
{
    if ( !playfabUser.bLoggedIn ) { return; }

    Json::Value json;
    json["scorestr"] = score;
    PlayFab::CloudScriptModels::ExecuteFunctionRequest request;
    request.FunctionName = "PostScore";
    request.GeneratePlayStreamEvent = false;
    request.CustomTags["default"] = "1";
    request.CustomTags["hash"] = hash;
    request.FunctionParameter = json;
    PlayFab::PlayFabCloudScriptAPI::ExecuteFunction(request, OnFunctionExecute, OnCloudScriptFailure,
        (void*)(intptr_t)(sequence));

    inprogress = true;
    postTick = ticks;
    PlayfabUser_t::PostScoreHandler_t::lastPostTicks = ticks;
}

bool PlayfabUser_t::LeaderboardData_t::LeaderBoard_t::errorReceivingLeaderboard()
{
    for ( auto& pair : awaitingResponse )
    {
        if ( pair.second.second != PlayFab::PlayFabErrorCode::PlayFabErrorUnknownError )
        {
            return true;
        }
    }
    return false;
}

void PlayfabUser_t::LeaderboardData_t::LeaderBoard_t::requestPlayerData(int start, int numEntries)
{
    if ( !playfabUser.bLoggedIn ) { return; }

    loading = true;

    Json::Value json;
    json["lid"] = name;
    int added = 0;

    int end = std::max(start, start + (numEntries - 1));
    int index = -1;
    for ( auto& entry : displayedRanks )
    {
        ++index;
        if ( index < start )
        {
            continue;
        }

        if ( index > end )
        {
            break;
        }

        if ( entry.awaitingData || entry.readIntoScore || entry.hasData )
        {
            continue;
        }

        json["players"].append(entry.id);
        entry.awaitingData = true;
        entry.hasData = false;

        ++added;
        if ( added == 50 )
        {
            PlayFab::CloudScriptModels::ExecuteFunctionRequest request;
            request.FunctionName = "LeaderboardGetPlayerData";
            request.GeneratePlayStreamEvent = false;
            request.CustomTags["default"] = "1";
            request.CustomTags["lid"] = name;
            request.FunctionParameter = json;

            playfabUser.leaderboardData.leaderboards[name].awaitingResponse[LeaderboardData_t::sequenceIDs] =
                std::make_pair(ticks, PlayFab::PlayFabErrorCode::PlayFabErrorUnknownError);

            PlayFab::PlayFabCloudScriptAPI::ExecuteFunction(request, OnFunctionExecute, OnCloudScriptFailure
                , (void*)(intptr_t)(LeaderboardData_t::sequenceIDs));
            ++LeaderboardData_t::sequenceIDs;

            json["players"].clear();
        }
    }

    if ( json["players"].size() > 0 )
    {
        PlayFab::CloudScriptModels::ExecuteFunctionRequest request;
        request.FunctionName = "LeaderboardGetPlayerData";
        request.GeneratePlayStreamEvent = false;
        request.CustomTags["default"] = "1";
        request.CustomTags["lid"] = name;
        request.FunctionParameter = json;

        playfabUser.leaderboardData.leaderboards[name].awaitingResponse[LeaderboardData_t::sequenceIDs] =
            std::make_pair(ticks, PlayFab::PlayFabErrorCode::PlayFabErrorUnknownError);

        PlayFab::PlayFabCloudScriptAPI::ExecuteFunction(request, OnFunctionExecute, OnCloudScriptFailure
            ,(void*)(intptr_t)(LeaderboardData_t::sequenceIDs));
        ++LeaderboardData_t::sequenceIDs;
    }
}

unsigned long djb2Hash2(char* str)
{
    unsigned long hash = 5381;
    int c;

    while ( c = *str++ )
    {
        hash = ((hash << 5) + hash) + c;    /* hash * 33 + c */
    }
    const char* str2 = BUILD_ENV_PFHID;
    while ( c = *str2++ )
    {
        hash = ((hash << 5) + hash) + c;    /* hash * 33 + c */
    }

    return hash;
}

void PlayfabUser_t::postScore(const int player)
{
#ifdef NDEBUG
{
    if ( conductGameChallenges[CONDUCT_CHEATS_ENABLED]
        || Mods::disableSteamAchievements
        || conductGameChallenges[CONDUCT_MODDED_NO_ACHIEVEMENTS])
    {
        return;
    }
    if ( gameStatistics[STATISTICS_DISABLE_UPLOAD] == 1 )
    {
        return;
    }
}
#endif

    SaveGameInfo info;
    info.populateFromSession(player);

    std::string scorestring = info.serializeToOnlineHiscore(player, victory);
    auto hash = djb2Hash2(const_cast<char*>(scorestring.c_str()));

    postScoreHandler.queue.push_back(PostScoreHandler_t::ScoreUpdate_t(scorestring, std::to_string(hash)));
    auto& entry = postScoreHandler.queue.back();
    entry.inprogress = false;
    entry.saveToFile();
}

bool PlayfabUser_t::PostScoreHandler_t::ScoreUpdate_t::saveToFile()
{
    std::string outputDir = "scores/processing/";
    if ( !PHYSFS_getRealDir(outputDir.c_str()) )
    {
        printlog("[JSON]: ScoreUpdate_t: %s directory not found", outputDir.c_str());
        return false;
    }
    std::string outputPath = PHYSFS_getRealDir(outputDir.c_str());
    outputPath.append(PHYSFS_getDirSeparator());
    outputPath.append(outputDir);
    outputPath.append(PHYSFS_getDirSeparator());

    auto t = getTime();
    struct tm* localTimeNow = std::localtime(&t);

    char buf[128] = "";
    snprintf(buf, sizeof(buf), "%4d%02d%02d_%02d%02d%02d.json",
        localTimeNow->tm_year + 1900, localTimeNow->tm_mon + 1, localTimeNow->tm_mday, localTimeNow->tm_hour, localTimeNow->tm_min, localTimeNow->tm_sec);

    std::string filename = hash;
    filename += "_";
    filename += std::to_string(sequence);
    filename += "_";
    filename += buf;

    outputPath.append(filename);

    rapidjson::Document d;
    d.SetObject();

    d.AddMember("version", rapidjson::Value(1), d.GetAllocator());
    d.AddMember("hash", rapidjson::Value(hash.c_str(), d.GetAllocator()), d.GetAllocator());
    d.AddMember("score", rapidjson::Value(score.c_str(), d.GetAllocator()), d.GetAllocator());

    File* fp = FileIO::open(outputPath.c_str(), "wb");
    if ( !fp )
    {
        printlog("[JSON]: Error opening json file %s for write!", outputPath.c_str());
        return false;
    }

    rapidjson::StringBuffer os;
    rapidjson::Writer<rapidjson::StringBuffer> writer(os);
    d.Accept(writer);
    fp->write(os.GetString(), sizeof(char), os.GetSize());
    FileIO::close(fp);

    writtenToFile = outputPath;

    printlog("[JSON]: Successfully wrote json file %s", outputPath.c_str());
    return true;
}

PlayfabUser_t::PostScoreHandler_t::ScoreUpdate_t::~ScoreUpdate_t()
{
    deleteFile();
}

char PlayfabUser_t::PostScoreHandler_t::buf[65535];
void PlayfabUser_t::PostScoreHandler_t::readFromFiles()
{
    std::string baseDir = "scores/processing/";
    if ( !PHYSFS_getRealDir(baseDir.c_str()) )
    {
        printlog("[JSON]: PostScoreHandler_t:: Error: Could not locate folder %s", baseDir.c_str());
        return;
    }

    for ( auto f : directoryContents("scores/processing/", false, true, outputdir) )
    {
        std::string inputPath = PHYSFS_getRealDir(baseDir.c_str());
        inputPath += "scores/processing/";
        inputPath += f;

        File* fp = FileIO::open(inputPath.c_str(), "rb");
        if ( !fp )
        {
            printlog("[JSON]: Error: Could not locate json file %s", inputPath.c_str());
            continue;
        }

        memset(buf, 0, sizeof(buf));
        int count = fp->read(buf, sizeof(buf[0]), sizeof(buf) - 1);
        buf[count] = '\0';
        rapidjson::StringStream is(buf);
        FileIO::close(fp);

        rapidjson::Document d;
        d.ParseStream(is);
        if ( !d.HasMember("version") || !d.HasMember("score") || !d.HasMember("hash") )
        {
            printlog("[JSON]: Error: No 'version' value in json file, or JSON syntax incorrect! %s", inputPath.c_str());
            continue;
        }

        int version = d["version"].GetInt();
        std::string score = d["score"].GetString();
        std::string hashStr = d["hash"].GetString();
        Uint32 hash = std::stoul(hashStr);

        queue.push_back(ScoreUpdate_t(score, hashStr));
        queue.back().writtenToFile = inputPath;

        if ( (Uint32)djb2Hash2(const_cast<char*>(score.c_str())) != hash )
        {
            printlog("[JSON]: Error: %s hash check failed", inputPath.c_str());
            queue.back().expired = true;
        }
    }
}

bool PlayfabUser_t::PostScoreHandler_t::ScoreUpdate_t::deleteFile()
{
    if ( writtenToFile != "" )
    {
        char path[PATH_MAX] = "";
        if ( access(writtenToFile.c_str(), F_OK) != -1 )
        {
            printlog("deleting score '%s'...\n", writtenToFile.c_str());
            int r = remove(writtenToFile.c_str());
            if ( r )
            {
                printlog("warning: failed to delete score in '%s'!\n", writtenToFile.c_str());
#ifdef _MSC_VER
                printlog(strerror(errno));
#endif
                return false;
            }
        }
    }
    return false;
}

void PlayfabUser_t::PostScoreHandler_t::update()
{
    bool anyInProgress = false;
    for ( auto it = queue.begin(); it != queue.end(); )
    {
        if ( it->expired )
        {
            it = queue.erase(it);
            continue;
        }

        if ( it->inprogress )
        {
            if ( (ticks - it->postTick) < TICKS_PER_SECOND * 60 ) // after 60 seconds allow a new post attempt
            {
                anyInProgress = true;
            }
        }
        ++it;
    }

    if ( !anyInProgress && ((ticks - lastPostTicks) > TICKS_PER_SECOND * 10) )
    {
        for ( auto it = queue.begin(); it != queue.end(); ++it)
        {
            if ( it->retryTicks == 0 || (ticks > it->retryTicks) )
            {
                it->post();
                break;
            }
        }
    }
}

void PlayfabUser_t::OnLeaderboardGet(const PlayFab::ClientModels::GetLeaderboardResult& result, void* customData)
{
    if ( result.Request.isMember("StatisticName") )
    {
        auto& leaderboard = playfabUser.leaderboardData.leaderboards[result.Request.asString()];
        leaderboard.ranks.clear();
        leaderboard.playerData.clear();
        for ( auto& entry : result.Leaderboard )
        {
            leaderboard.ranks.push_back(LeaderboardData_t::LeaderBoard_t::Entry_t());
            auto& rank = leaderboard.ranks.back();
            rank.displayName = entry.DisplayName;
            rank.rank = entry.Position;
            rank.id = entry.PlayFabId;
            rank.score = entry.StatValue;
        }
    }
}

void PlayfabUser_t::OnLeaderboardFail(const PlayFab::PlayFabError& error, void* customData)
{
    logError("Failed to obtain leaderboard: %s", error.ErrorMessage.c_str());

    if ( error.Request["CustomTags"].isMember("lid") )
    {
        int sequence = reinterpret_cast<intptr_t>(customData);
        std::string lid = error.Request["CustomTags"]["lid"].asString();
        auto& leaderboard = playfabUser.leaderboardData.leaderboards[lid];
        if ( leaderboard.awaitingResponse.find(sequence) != leaderboard.awaitingResponse.end() )
        {
            leaderboard.awaitingResponse[sequence].second = error.ErrorCode;
            leaderboard.loading = false;
        }
    }
}

void PlayfabUser_t::OnLeaderboardAroundMeGet(const PlayFab::ClientModels::GetLeaderboardAroundPlayerResult& result, void* customData)
{
    if ( result.Request["CustomTags"].isMember("lid") )
    {
        std::string lid = result.Request["CustomTags"]["lid"].asString();
        auto& leaderboard = playfabUser.leaderboardData.leaderboards[lid];
        leaderboard.name = lid;

        int sequence = reinterpret_cast<intptr_t>(customData);
        if ( leaderboard.awaitingResponse.find(sequence) != leaderboard.awaitingResponse.end() )
        {
            leaderboard.awaitingResponse.erase(sequence);

            if ( intro )
            {
                leaderboard.displayedRanks.clear();
                leaderboard.playerData.clear();
                for ( auto& entry : result.Leaderboard )
                {
                    leaderboard.displayedRanks.push_back(LeaderboardData_t::LeaderBoard_t::Entry_t());
                    auto& rank = leaderboard.displayedRanks.back();
                    rank.displayName = entry.DisplayName;
                    rank.rank = entry.Position;
                    rank.id = entry.PlayFabId;
                    rank.score = entry.StatValue;
                }

                if ( leaderboard.awaitingResponse.empty() )
                {
                    leaderboard.loading = false;
                    leaderboard.requestPlayerData(0, 10);
                }
            }
        }
    }
}

void PlayfabUser_t::getLeaderboardAroundMe(std::string lid)
{
    if ( !bLoggedIn ) { return; }

    if ( leaderboardData.leaderboards[lid].loading )
    {
        for ( auto& pair : leaderboardData.leaderboards[lid].awaitingResponse )
        {
            if ( (ticks - pair.second.first) < 5 * TICKS_PER_SECOND )
            {
                return; // waiting on a leaderboard to load in
            }
        }
    }

    leaderboardData.leaderboards.erase(lid);

    PlayFab::ClientModels::GetLeaderboardAroundPlayerRequest request;
    request.StatisticName = lid;
    request.MaxResultsCount = 100;
    request.PlayFabId = currentUser;
    request.CustomTags["lid"] = lid;

    PlayFab::ClientModels::PlayerProfileViewConstraints constraints;
    constraints.ShowDisplayName = true;
    request.ProfileConstraints = constraints;
    
    playfabUser.leaderboardData.leaderboards[lid].awaitingResponse[LeaderboardData_t::sequenceIDs] =
        std::make_pair(ticks, PlayFab::PlayFabErrorCode::PlayFabErrorUnknownError);

    PlayFab::PlayFabClientAPI::GetLeaderboardAroundPlayer(request, OnLeaderboardAroundMeGet, OnLeaderboardFail,
        (void*)(intptr_t)(LeaderboardData_t::sequenceIDs));
    ++LeaderboardData_t::sequenceIDs;
}

void PlayfabUser_t::resetLogin()
{
    playfabUser.loggingIn = false;
    playfabUser.bLoggedIn = false;
    playfabUser.errorLogin = false;
}

void PlayfabUser_t::update()
{
    if ( !bInit ) { return; }
    bool tickUpdate = false;
    if ( processedOnTick != ticks )
    {
        processedOnTick = ticks;
        tickUpdate = true;
    }

    if ( tickUpdate )
    {
        if ( authenticationRefresh > 0 )
        {
            --authenticationRefresh;
            if ( authenticationRefresh == 0 )
            {
                resetLogin();
            }
        }
    }

    if ( !loggingIn && !bLoggedIn && !errorLogin )
    {
        if ( type == PlayerType_Epic )
        {
            loginEpic();
        }
        else if ( type == PlayerType_Steam )
        {
            loginSteam();
        }
    }

    if ( bLoggedIn && type == PlayerType_Epic )
    {
#ifdef USE_EOS
        if ( currentUserName == "" 
            && EOS.CurrentUserInfo.bUserLoggedIn 
            && !EOS.CurrentUserInfo.bUserInfoRequireUpdate )
        {
            if ( EOS.CurrentUserInfo.Name != "" )
            {
                PlayFab::ClientModels::UpdateUserTitleDisplayNameRequest request;
                request.DisplayName = EOS.CurrentUserInfo.Name;
                request.DisplayName = request.DisplayName.substr(0, 25);

                currentUserName = EOS.CurrentUserInfo.Name;
                if ( request.DisplayName != "" )
                {
                    PlayFab::PlayFabClientAPI::UpdateUserTitleDisplayName(request, OnDisplayNameUpdateSuccess);
                }
            }
        }
#endif
    }

    playfabUser.postScoreHandler.update();
}

void PlayfabUser_t::init()
{
    if ( bInit ) { return; }
    PlayFab::PlayFabSettings::staticSettings->titleId = BUILD_ENV_PFTID;
    bInit = true;

#ifdef STEAMWORKS
    type = PlayerType_Steam;
#elif defined(NINTENDO)
    type = PlayerType_Nintendo;
#elif defined(USE_EOS)
    type = PlayerType_Epic;
#else
    type = PlayerType_None;
#endif // STEAMWORKS

    postScoreHandler.readFromFiles();
}

std::string PlayfabUser_t::LeaderboardSearch_t::getLeaderboardDisplayName()
{
    std::string period = daily ? "Daily " : "All-Time ";
    std::string results = scoresNearMe ? "Entries Near Me" : "Top 100";

    std::string score = scoreType == RANK_TOTALSCORE ? "Total Score" : "Fastest Time";
    if ( win == false )
    {
        score = "Furthest Depth";
    }

    if ( daily )
    {
        return period + results + "\n" + (win ? "Wins" : "Losses") + " - Ranked by: " + score;
    }
    std::string victoryStr;

    int victory = this->victory;
    if ( win == false ) 
    { 
        victory = 0; 
    }
    else
    {
        victory = std::max(1, std::min(victory, 3));
    }
    switch ( victory )
    {
    case 0:
        victoryStr = "";
        break;
    case 1:
        victoryStr = "(Classic)";
        break;
    case 2:
        victoryStr = "(Classic - Hell)";
        break;
    case 3:
        victoryStr = "(Citadel)";
        break;
    default:
        if ( victory > 3 )
        {
            victoryStr = "(Citadel)";
        }
        break;
    }

    std::string scenario = (win ? "Wins " + victoryStr: "Losses");

    std::string hcStr = hardcore ? "Hardcore" : "Normal";
    std::string multiStr = multiplayer ? "Co-op" : "Solo";
    std::string monsterStr = dlc ? "Monsters" : "Humans";

    std::string res = period + results;
    res += '\n';
    res += scenario + " - Ranked by: " + score;
    res += '\n';
    
    res += "(" + multiStr + ", " + monsterStr + ", " + hcStr + ")";
    return res;
}

#endif