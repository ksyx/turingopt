package main

import (
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

type Result struct {
	// map[machine_name]ContentID
	CommonContent map[string]int `json:"common_content"`
	// map[machine_name](map[user]HTML)
	UserContent map[string](map[string]string) `json:"user_content"`
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

var ErrorMismatch = errors.New("input mismatch from expectation")

func loadResult(dir string, id int, name string) error {
	dir = dir + "/" + name + ".mail"
	f, err := os.Open(dir + ".header")
	if err != nil {
		return err
	}
	bytes, err := io.ReadAll(f)
	if err != nil {
		return err
	}
	content := string(bytes)
	ind := strings.Index(content, "<head>")
	if ind == -1 {
		return ErrorMismatch
	}
	content = content[ind:]
	f.Close()
	f, err = os.Open(dir)
	if err != nil {
		return err
	}
	bytes, err = io.ReadAll(f)
	if err != nil {
		return err
	}
	content = content + string(bytes)
	strid := fmt.Sprint(id)
	content = strings.ReplaceAll(content, strid+":"+name, strid+":json")
	f.Close()

	doc, err := goquery.NewDocumentFromReader(strings.NewReader(content))
	if err != nil {
		return err
	}
	cur := doc.Find("body").First().Children().First()
	secname := "Greeting"
	machine_name := "greeting"
	exact := map[string]bool{
		"greeting": true,
		"news":     true,
		"usage":    true,
		"footer":   true,
	}
	suffixes := []string{
		"_metrics",
		"_problems",
	}
	for cur.Length() > 0 {
		sel := cur.NextUntil("h1, h2, h3, h4, h5, h6, a[name=\"footer\"]")
		htmlcur, err := goquery.OuterHtml(cur)
		if err != nil {
			return err
		}
		htmlsel, err := goquery.OuterHtml(sel)
		if err != nil {
			return err
		}
		html := htmlcur + htmlsel
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
		machine_name, ok = cur.Attr("name")
		if !ok {
			machine_name, ok = cur.Children().First().Attr("name")
			if !ok {
				return ErrorMismatch
			}
		} else {
			secname = "Footer"
		}
	}
	seenUser[name] = true
	return nil
}

func loadResultDir(dir string, id int) error {
	dir = analysisResultPathBase + "/" + dir
	entries, err := os.ReadDir(dir)
	if err != nil {
		return err
	}
	last := ""
	action := func(user string) error {
		if user != "" {
			err := loadResult(dir, id, user)
			if err != nil {
				return err
			}
		}
		return nil
	}
	result := Result{
		CommonContent: make(map[string]int),
		UserContent:   make(map[string](map[string]string)),
	}
	resultSet.Results[id] = result
	for _, entry := range entries {
		if !entry.IsDir() {
			name := entry.Name()
			user := strings.TrimSuffix(name, ".mail")
			if len(user) != len(name) {
				if last != "" {
					loadResult(dir, id, last)
				}
				last = user
			} else if strings.HasSuffix(name, ".empty") {
				last = ""
			}
		}
	}
	action(last)
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
		if d.IsDir() {
			name := d.Name()
			id, err := strconv.Atoi(name)
			if err != nil {
				continue
			}
			if err := loadResultDir(name, id); err != nil {
				logger.Error(err.Error())
				continue
			}
			resultsAvailable[id] = true
		}
	}
	jsonbytes, err := json.MarshalIndent(resultSet, "", "  ")
	if err != nil {
		logger.Fatal(err.Error())
	}
	fmt.Println(string(jsonbytes))
	//serve()
}
