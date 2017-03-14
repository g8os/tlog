package main

import (
	"flag"
	"log"

	"github.com/g8os/tlog/client"
)

var (
	numClient int
	numFlush  int
)

func main() {
	flag.IntVar(&numClient, "num_client", 2, "number of clients")
	flag.IntVar(&numFlush, "num_flush", 40, "number of flush")

	flag.Parse()

	var volID uint32 = 0x1f

	clients := make([]*client.Client, numClient)
	clientReady := make(chan int, numClient)
	seqChan := make(chan uint64, 8)

	for i := 0; i < numClient; i++ {
		client, err := client.New("127.0.0.1:11211")
		if err != nil {
			log.Fatal(err)
		}
		clients[i] = client
		clientReady <- i
	}
	data := make([]byte, 4096*4)

	// produce the data
	go func() {
		for i := 0; i < 25*numFlush; i++ {
			seqChan <- uint64(i)
		}
	}()

	for seq := range seqChan {
		idx := <-clientReady
		func(j uint64, idx int) {
			client := clients[int(j)%numClient]

			log.Printf("j=%v\n", j)
			err := client.Send(volID, j, uint32(j), j, j, data)
			if err != nil {
				log.Printf("client %v died\n", idx)
				return
			}
			clientReady <- idx
		}(seq, idx)
	}
}
