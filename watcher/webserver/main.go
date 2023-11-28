package main

import (
	"archive/tar"
	"compress/gzip"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log"
	"os"
	"strconv"
	"strings"

	"github.com/PuerkitoBio/goquery"
	"go.uber.org/zap"
)

type ResultRaw struct {
	Started int64                  `json:"started"`
	Updated int64                  `json:"updated"`
	Data    map[string]interface{} `json:"data"`
}

type Result struct {
	// map[machine_name]ContentID
	CommonContent map[string]int `json:"common_content"`
	// map[machine_name](map[user]HTML)
	UserContent map[string](map[string]string) `json:"user_content"`
	RawData     ResultRaw                      `json:"-"`
}

type CommonContentDedup struct {
	ContentID   map[string]int `json:"-"`
	ContentData map[int]string `json:"content_data"`
	Size        int            `json:"-"`
}

type ResultSet struct {
	Dedup       CommonContentDedup `json:"dedup"`
	Results     map[int]Result     `json:"results"`
	NameMapping map[string]string  `json:"name_mapping"`
}

var resultSet = ResultSet{
	Dedup: CommonContentDedup{
		ContentID:   make(map[string]int),
		ContentData: make(map[int]string),
	},
	Results:     make(map[int]Result),
	NameMapping: make(map[string]string),
}

var resultsAvailable map[int]bool
var seenUser map[string]bool

var logger *zap.Logger

func genMismatchError(msg string) error {
	return errors.New(msg + ": input mismatch from expectation")
}

func loadResult(headerText string, bodyText string, id int, name string) error {
	ind := strings.Index(headerText, "<head>")
	if ind == -1 {
		return genMismatchError("find_head")
	}
	headerText = headerText[ind:]
	bodyText = headerText + bodyText
	content := bodyText
	strid := fmt.Sprint(id)
	content = strings.ReplaceAll(content, strid+":"+name, strid+":web")

	doc, err := goquery.NewDocumentFromReader(strings.NewReader(content))
	if err != nil {
		return err
	}
	exact := map[string]bool{
		"greeting": true,
		"news":     true,
		"usage":    true,
		"footer":   true,
	}
	cur := doc.Find("a[name$=\"_problems\"]").Each(
		func(_ int, sel *goquery.Selection) {
			tag, _ := sel.First().Attr("name")
			tag = strings.TrimSuffix(tag, "_problems")
			exact[tag] = true
		})
	footer_parent := doc.Find("a[name=\"footer\"]").Parent()
	breakpoints := doc.Find("h1, h2, h3, h4, h5, h6").Union(footer_parent)
	cur = doc.Find("body>*").First()
	greet_html, err := cur.Html()
	if err != nil {
		return err
	}
	cur.SetHtml("<a name=\"greeting\"></a>" + greet_html)
	secname := "Greeting"
	machine_name := "greeting"
	suffixes := []string{
		"_metrics",
		"_problems",
	}
	for cur.Length() > 0 {
		var sel *goquery.Selection
		htmltail := ""
		if machine_name == "footer" {
			secname = "Footer"
			lastch := doc.Find("body").Children().Last()
			htmltail, err = goquery.OuterHtml(doc.Find("body").Children().Last())
			if err != nil {
				return err
			}
			sel = cur.NextUntilSelection(lastch)
		} else {
			sel = cur.NextUntilSelection(breakpoints)
		}
		htmlcur, err := goquery.OuterHtml(cur)
		if err != nil {
			return err
		}
		htmlsel, err := goquery.OuterHtml(sel)
		if err != nil {
			return err
		}
		html := htmlcur + htmlsel + htmltail
		_, ok := exact[machine_name]
		if !ok {
			for _, suffix := range suffixes {
				if strings.HasSuffix(machine_name, suffix) {
					ok = true
					break
				}
			}
		}
		if ok {
			contentid, ok := resultSet.Dedup.ContentID[html]
			if !ok {
				resultSet.Dedup.Size += 1
				contentid = resultSet.Dedup.Size
				resultSet.Dedup.ContentData[contentid] = html
				resultSet.Dedup.ContentID[html] = contentid
			}
			resultSet.Results[id].CommonContent[machine_name] = contentid
		} else {
			if resultSet.Results[id].UserContent[machine_name] == nil {
				resultSet.Results[id].UserContent[machine_name] =
					make(map[string]string)
			}
			resultSet.Results[id].UserContent[machine_name][name] = html
		}
		resultSet.NameMapping[machine_name] = secname
		cur = sel.Last().Next()
		secname = cur.Text()
		machine_name, ok = cur.Children().First().Attr("name")
		if !ok {
			return genMismatchError("get_machine_name")
		}
	}
	seenUser[name] = true
	return nil
}

func loadResultTar(tarReader *tar.Reader, id int) error {
	info, err := tarReader.Next()
	if err != nil {
		return err
	}
	if info.Name != "raw.json" {
		return genMismatchError("find_raw_json")
	}
	data, err := io.ReadAll(tarReader)
	if err != nil {
		return err
	}
	result := Result{
		CommonContent: make(map[string]int),
		UserContent:   make(map[string](map[string]string)),
		RawData:       ResultRaw{},
	}
	err = json.Unmarshal(data, &result.RawData)
	if err != nil {
		return err
	}
	resultSet.Results[id] = result
	curName := ""
	var headerText []byte
	var bodyText []byte
	action := func(name string) error {
		if curName != "" {
			return loadResult(string(headerText), string(bodyText), id, name)
		}
		return nil
	}
	for {
		info, err := tarReader.Next()
		if err != nil {
			if err == io.EOF {
				break
			} else {
				return err
			}
		}
		name := info.Name
		ind := strings.IndexRune(name, '.')
		if ind == -1 || ind == len(name)-1 {
			return genMismatchError("find_filename_ext")
		}
		ext := name[ind:]
		name = name[0:ind]
		for i := len(ext) - 2; i >= 0; i-- {
			if ext[i] == '.' {
				ind = i
				break
			}
		}
		ext = ext[ind+1:]
		switch ext {
		case "header":
			if name != curName {
				action(curName)
				curName = name
			}
			headerText, err = io.ReadAll(tarReader)
			if err != nil {
				return err
			}
		case "mail":
			bodyText, err = io.ReadAll(tarReader)
			if err != nil {
				return err
			}
		case "empty":
			curName = ""
		}
	}
	action(curName)
	return nil
}

func main() {
	var err error
	logger, err = zap.NewProduction()
	if err != nil {
		log.Panic(err)
	}
	dirlist, err := os.ReadDir(analysisResultPathBase)
	if err != nil {
		logger.Fatal(err.Error())
	}
	resultsAvailable = make(map[int]bool)
	seenUser = make(map[string]bool)
	for _, d := range dirlist {
		if d.Type().IsRegular() {
			name := d.Name()
			namelen := len(name)
			name = strings.TrimSuffix(name, ".tar.gz")
			if namelen == len(name) {
				continue
			}
			id, err := strconv.Atoi(name)
			if err != nil {
				logger.Error(err.Error())
				continue
			}
			fileReader, err := os.Open(analysisResultPathBase + "/" + d.Name())
			if err != nil {
				logger.Error(err.Error())
				continue
			}
			gzipReader, err := gzip.NewReader(fileReader)
			if err != nil {
				logger.Error(err.Error())
				continue
			}
			tarReader := tar.NewReader(gzipReader)
			if err := loadResultTar(tarReader, id); err != nil {
				logger.Error(err.Error())
				continue
			}
			gzipReader.Close()
			fileReader.Close()
			resultsAvailable[id] = true
		}
	}
	const dump_data_json_only = false
	if dump_data_json_only {
		jsonbytes, err := json.MarshalIndent(resultSet, "", "  ")
		if err != nil {
			logger.Fatal(err.Error())
		}
		fmt.Println(string(jsonbytes))
		return
	}
	//serve()
}
