package interp

import (
	"fmt"
	"io"
	"net/http"
	"strings"
	"time"

	"github.com/bluesentinelsec/basl/pkg/basl/value"
)

// ── http module ──

func (interp *Interpreter) makeHttpModule() *Env {
	env := NewEnv(nil)

	env.Define("get", value.NewNativeFunc("http.get", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("http.get: expected string url")
		}
		return doHTTP("GET", args[0].AsString(), "", nil)
	}))

	env.Define("post", value.NewNativeFunc("http.post", func(args []value.Value) (value.Value, error) {
		if len(args) != 2 || args[0].T != value.TypeString || args[1].T != value.TypeString {
			return value.Void, fmt.Errorf("http.post: expected (string url, string body)")
		}
		return doHTTP("POST", args[0].AsString(), args[1].AsString(), nil)
	}))

	env.Define("request", value.NewNativeFunc("http.request", func(args []value.Value) (value.Value, error) {
		if len(args) < 3 {
			return value.Void, fmt.Errorf("http.request: expected (string method, string url, map<string,string> headers, string body)")
		}
		if args[0].T != value.TypeString || args[1].T != value.TypeString {
			return value.Void, fmt.Errorf("http.request: method and url must be strings")
		}
		var headers map[string]string
		if args[2].T == value.TypeMap {
			headers = make(map[string]string)
			m := args[2].AsMap()
			for i, k := range m.Keys {
				headers[k.AsString()] = m.Values[i].AsString()
			}
		}
		body := ""
		if len(args) > 3 && args[3].T == value.TypeString {
			body = args[3].AsString()
		}
		return doHTTP(args[0].AsString(), args[1].AsString(), body, headers)
	}))

	env.Define("listen", value.NewNativeFunc("http.listen", func(args []value.Value) (value.Value, error) {
		if len(args) != 2 || args[0].T != value.TypeString || args[1].T != value.TypeFunc {
			return value.Void, fmt.Errorf("http.listen: expected (string addr, fn handler)")
		}
		addr := args[0].AsString()
		handlerVal := args[1]
		handler := http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			bodyBytes, _ := io.ReadAll(r.Body)
			// Build request object
			hdrs := &value.MapVal{}
			for k := range r.Header {
				hdrs.Keys = append(hdrs.Keys, value.NewString(k))
				hdrs.Values = append(hdrs.Values, value.NewString(r.Header.Get(k)))
			}
			reqObj := &value.ObjectVal{
				ClassName: "HttpRequest",
				Fields: map[string]value.Value{
					"method":  value.NewString(r.Method),
					"path":    value.NewString(r.URL.Path),
					"query":   value.NewString(r.URL.RawQuery),
					"body":    value.NewString(string(bodyBytes)),
					"headers": {T: value.TypeMap, Data: hdrs},
				},
			}
			reqVal := value.Value{T: value.TypeObject, Data: reqObj}
			result, err := interp.callFunc(handlerVal, []value.Value{reqVal})
			if err != nil {
				w.WriteHeader(500)
				w.Write([]byte("handler error"))
				return
			}
			// Expect result to be HttpResponse object with status, headers, body
			if result.T == value.TypeObject {
				resp := result.AsObject()
				if sv, ok := resp.Fields["status"]; ok {
					w.WriteHeader(int(sv.AsI32()))
				}
				if bv, ok := resp.Fields["body"]; ok {
					w.Write([]byte(bv.AsString()))
				}
			}
		})
		err := http.ListenAndServe(addr, handler)
		if err != nil {
			return value.NewErr(err.Error(), value.ErrKindIO), nil
		}
		return value.Ok, nil
	}))

	return env
}

func doHTTP(method, url, body string, headers map[string]string) (value.Value, error) {
	var bodyReader io.Reader
	if body != "" {
		bodyReader = strings.NewReader(body)
	}
	req, err := http.NewRequest(method, url, bodyReader)
	if err != nil {
		return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, value.NewErr(err.Error(), value.ErrKindIO)}}
	}
	for k, v := range headers {
		req.Header.Set(k, v)
	}
	client := &http.Client{Timeout: 30 * time.Second}
	resp, err := client.Do(req)
	if err != nil {
		return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, value.NewErr(err.Error(), value.ErrKindIO)}}
	}
	defer resp.Body.Close()
	respBody, err := io.ReadAll(resp.Body)
	if err != nil {
		return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, value.NewErr(err.Error(), value.ErrKindIO)}}
	}
	hdrs := &value.MapVal{}
	for k := range resp.Header {
		hdrs.Keys = append(hdrs.Keys, value.NewString(k))
		hdrs.Values = append(hdrs.Values, value.NewString(resp.Header.Get(k)))
	}
	obj := &value.ObjectVal{
		ClassName: "HttpResponse",
		Fields: map[string]value.Value{
			"status":  value.NewI32(int32(resp.StatusCode)),
			"body":    value.NewString(string(respBody)),
			"headers": {T: value.TypeMap, Data: hdrs},
		},
	}
	return value.Void, &MultiReturnVal{Values: []value.Value{{T: value.TypeObject, Data: obj}, value.Ok}}
}
