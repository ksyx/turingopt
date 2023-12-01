package main

import (
	"compress/gzip"
	"encoding/json"
	"io"
	"os"
	"strconv"

	"net/http"
	"strings"
)

const analysisResultPathBase = "analysis_result"
const apiBaseUrl = "/api"
const userAuthResultField = "X-AuthUser"

var isAdmin map[string]bool

func composeResponse(
	w http.ResponseWriter, respCode int, msg string, payload interface{}) {
	var data struct {
		Success  bool        `json:"ok"`
		Msg      string      `json:"msg"`
		Response interface{} `json:"payload"`
	}
	success := respCode == http.StatusOK
	data.Success = success
	data.Msg = msg
	data.Response = payload
	bytes, err := json.Marshal(data)
	if err != nil {
		logger.Error(err.Error())
		w.WriteHeader(http.StatusInternalServerError)
		return
	}
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Content-Encoding", "gzip")
	w.WriteHeader(respCode)
	gzipWriter := gzip.NewWriter(w)
	defer gzipWriter.Close()
	gzipWriter.Write(bytes)
}

func authAdmin(user string) bool {
	val, ok := isAdmin[user]
	return val && ok
}

func getUser(r *http.Request) (string, bool) {
	user := r.Header.Get(userAuthResultField)
	atSymbol := strings.IndexRune(user, '@')
	if atSymbol != -1 {
		user = user[0:atSymbol]
	}
	return user, authAdmin(user)
}

func checkPeriodAvailability(period int) (bool, bool, Result) {
	avail, ok := resultsAvailable[period]
	if !avail || !ok {
		return false, false, Result{}
	}
	result, ok := resultSet.Results[period]
	if !ok {
		return true, false, Result{}
	}
	return true, true, result
}

func getRawData(w http.ResponseWriter, r *http.Request) {
	lock.RLock()
	defer lock.RUnlock()
	if r.Method != "POST" {
		composeResponse(w, http.StatusMethodNotAllowed, "POST required", nil)
		return
	}
	if r.ContentLength > 10240 {
		composeResponse(w, http.StatusRequestEntityTooLarge, "Request too long", nil)
		return
	}
	var request struct {
		Periods []int `json:"periods"`
	}
	bodyBytes, err := io.ReadAll(r.Body)
	if err != nil {
		composeResponse(w, http.StatusBadRequest, "Cannot read request body", nil)
		return
	}
	err = json.Unmarshal(bodyBytes, &request)
	if err != nil {
		composeResponse(w, http.StatusBadRequest, "JSON posted could not be parsed", nil)
		return
	}
	response := make(map[int]ResultRaw)
	msg := ""
	user, isAdmin := getUser(r)
	addMsg := func(msgContent string, period int) {
		msg += msgContent + ": " + strconv.Itoa(period) + ";"
	}
	for _, period := range request.Periods {
		found, available, result := checkPeriodAvailability(period)
		if !found || !available {
			var msgToAdd string
			if !found {
				msgToAdd = "period not found"
			} else {
				msgToAdd = "period unavailable"
			}
			addMsg(msgToAdd, period)
			continue
		}
		if isAdmin {
			response[period] = result.RawData
		} else {
			curUserRaw, ok := result.RawData.Data[user]
			if !ok {
				addMsg("no data for user in period", period)
				continue
			}
			curRawData := ResultRaw{
				Started: result.RawData.Started,
				Updated: result.RawData.Updated,
				Data:    make(map[string]interface{}),
			}
			curRawData.Data[user] = curUserRaw
			response[period] = curRawData
		}
	}
	if len(response) == 0 {
		if len(request.Periods) == 0 {
			msg = "no period specified"
		}
		composeResponse(w, http.StatusNotFound, msg, "")
		return
	}
	composeResponse(w, http.StatusOK, msg, response)
}

func getReport(w http.ResponseWriter, r *http.Request) {
	lock.RLock()
	defer lock.RUnlock()
	period := r.URL.Query().Get("period")
	if period == "" {
		composeResponse(w, http.StatusBadRequest, "Missing param period", nil)
		return
	}
	periodId, err := strconv.Atoi(period)
	if err != nil {
		composeResponse(w, http.StatusBadRequest, "Nonintegral param period", nil)
		return
	}
	found, available, result := checkPeriodAvailability(periodId)
	if !found || !available {
		var msg string
		if !found {
			msg = "Period not found"
		} else {
			msg = "Period unavailable"
		}
		composeResponse(w, http.StatusNotFound, msg, nil)
		return
	}
	resultSet := ResultSet{
		Dedup:       resultSet.Dedup,
		Results:     make(map[int]Result),
		NameMapping: resultSet.NameMapping,
	}
	user, isAdmin := getUser(r)
	if isAdmin {
		resultSet.Results[periodId] = result
	} else {
		seenUser, ok := result.seenUser[user]
		if !ok || !seenUser {
			composeResponse(w, http.StatusNotFound, "No data for user in this period", nil)
			return
		}
		curResult := Result{
			CommonContent: result.CommonContent,
			UserContent:   make(map[string](map[string]string)),
		}
		for key, val := range result.UserContent {
			data, ok := val[user]
			if !ok {
				continue
			}
			curResult.UserContent[key] = make(map[string]string)
			curResult.UserContent[key][user] = data
		}
		resultSet.Results[periodId] = curResult
	}
	composeResponse(w, http.StatusOK, "", resultSet)
}

func listPeriods(w http.ResponseWriter, r *http.Request) {
	lock.RLock()
	defer lock.RUnlock()
	type responseTy struct {
		Started int64 `json:"started"`
		Updated int64 `json:"updated"`
	}
	response := make(map[int]responseTy)
	user, isAdmin := getUser(r)
	for period, avail := range resultsAvailable {
		if avail {
			result, ok := resultSet.Results[period]
			if !ok {
				logger.Warn("Period " + strconv.Itoa(period) + " is marked as" +
					" available but unable to find entry in resultSet.Results")
				continue
			}
			if !isAdmin {
				seenUser, ok := result.seenUser[user]
				if !ok || !seenUser {
					continue
				}
			}
			curResponse := responseTy{
				Started: result.RawData.Started,
				Updated: result.RawData.Updated,
			}
			response[period] = curResponse
		}
	}
	composeResponse(w, http.StatusOK, "", response)
}

func user(w http.ResponseWriter, r *http.Request) {
	user, isAdmin := getUser(r)
	var response struct {
		User    string `json:"user"`
		IsAdmin bool   `json:"admin"`
	}
	response.User = user
	response.IsAdmin = isAdmin
	composeResponse(w, http.StatusOK, "", response)
}

func serve() {
	configFile, err := os.Open("config.json")
	if err != nil {
		logger.Fatal(err.Error())
	}
	configBytes, err := io.ReadAll(configFile)
	if err != nil {
		logger.Fatal(err.Error())
	}
	var config struct {
		Admin []string `json:"admin"`
	}
	err = json.Unmarshal(configBytes, &config)
	if err != nil {
		logger.Fatal(err.Error())
	}
	isAdmin = make(map[string]bool)
	for _, name := range config.Admin {
		isAdmin[name] = true
	}
	mux := http.NewServeMux()
	mux.HandleFunc(apiBaseUrl+"/user", user)
	mux.HandleFunc(apiBaseUrl+"/periods", listPeriods)
	mux.HandleFunc(apiBaseUrl+"/report", getReport)
	mux.HandleFunc(apiBaseUrl+"/data", getRawData)
	http.ListenAndServe(":8080", mux)
}
